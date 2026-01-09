"""
Simulation class for ef5py.

High-level interface to run EF5 simulations.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Union
import numpy as np

try:
    from . import core
    HAS_CORE = True
except ImportError:
    HAS_CORE = False

from .grid import Grid
from .config import Config


@dataclass
class SimulationResults:
    """Results from a simulation timestep or run."""
    fast_flow: np.ndarray
    slow_flow: np.ndarray
    soil_moisture: np.ndarray
    channel_flow: Optional[np.ndarray] = None
    overland_flow: Optional[np.ndarray] = None
    actual_et: Optional[np.ndarray] = None
    diagnostics: Optional[dict] = None


class Simulation:
    """
    High-level EF5 simulation interface.
    
    Manages model parameters, states, and execution.
    
    Parameters
    ----------
    grid : Grid
        Spatial domain
    model : str
        Water balance model ('CREST', 'SAC', 'HP')
    routing : str
        Routing model ('kinematic', 'linear_reservoir')
        
    Examples
    --------
    >>> grid = Grid.from_tifs("dem.tif", "fdir.tif")
    >>> sim = Simulation(grid, model="CREST")
    >>> sim.set_parameters(wm=150.0, b=0.4, fc=1.5)
    >>> result = sim.step(precip, pet, dt=1.0)
    """
    
    def __init__(
        self,
        grid: Grid,
        model: str = "CREST",
        routing: str = "kinematic"
    ):
        if not HAS_CORE:
            raise ImportError("ef5py core not available")
        
        self.grid = grid
        self.model_name = model.upper()
        self.routing_name = routing.lower()
        
        self.n_cells = grid.n_cells
        
        # Initialize model components
        self._init_water_balance()
        self._init_routing()
        
        self._initialized = False
    
    def _init_water_balance(self):
        """Initialize water balance model."""
        if self.model_name == "CREST":
            self.wb_params = core.crest.make_params(self.n_cells)
            self.wb_states = core.crest.make_states(self.n_cells)
        else:
            raise ValueError(f"Unknown model: {self.model_name}")
    
    def _init_routing(self):
        """Initialize routing model."""
        if self.routing_name == "kinematic":
            self.rt_params = core.kinematic.make_params(self.n_cells)
            self.rt_states = core.kinematic.make_states(self.n_cells)
        else:
            # No routing
            self.rt_params = None
            self.rt_states = None
    
    def set_parameters(self, **kwargs):
        """
        Set model parameters.
        
        Parameters can be:
        - Scalars: applied to all cells
        - Arrays: one value per cell
        - Paths: loaded from TIF files
        
        CREST Parameters
        ----------------
        wm : float or array
            Maximum soil water capacity (mm)
        b : float or array
            Exponent of VIC
        im : float or array
            Impervious fraction (0-1)
        ke : float or array
            Overland flow coefficient
        fc : float or array
            Saturated hydraulic conductivity (mm/hr)
        iwu : float or array
            Initial soil moisture fraction (0-1)
        ksat : float or array
            Groundwater hydraulic conductivity
        """
        for key, value in kwargs.items():
            self._set_param(key, value)
    
    def _set_param(self, name: str, value):
        """Set a single parameter."""
        name = name.lower()
        
        # Convert value to array if needed
        if isinstance(value, str):
            # Load from file
            value = self._load_param_file(value)
        elif np.isscalar(value):
            value = np.full(self.n_cells, value, dtype=np.float32)
        else:
            value = np.asarray(value, dtype=np.float32)
        
        if len(value) != self.n_cells:
            raise ValueError(f"Parameter {name} has {len(value)} values, expected {self.n_cells}")
        
        # CREST parameters
        crest_params = {'wm', 'b', 'im', 'ke', 'fc', 'iwu', 'ksat'}
        if name in crest_params:
            for i, p in enumerate(self.wb_params):
                setattr(p, name, float(value[i]))
            return
        
        # Kinematic parameters
        kw_params = {'alpha', 'alpha0', 'beta', 'threshold', 'leak_i', 'under', 'isu'}
        if name in kw_params and self.rt_params is not None:
            for i, p in enumerate(self.rt_params):
                setattr(p, name, float(value[i]))
            return
        
        raise ValueError(f"Unknown parameter: {name}")
    
    def _load_param_file(self, path: str) -> np.ndarray:
        """Load parameter from raster file."""
        try:
            import rasterio
        except ImportError:
            raise ImportError("rasterio required to load parameter files")
        
        with rasterio.open(path) as src:
            data = src.read(1)
        
        # Extract values at valid cell locations
        indices = self.grid.get_cell_indices()
        values = data[indices[:, 0], indices[:, 1]]
        return values.astype(np.float32)
    
    def initialize(self):
        """Initialize model states from parameters."""
        core.crest.initialize_states(self.wb_params, self.wb_states)
        
        if self.rt_params is not None:
            core.kinematic.initialize_states(self.rt_params, self.rt_states)
        
        self._initialized = True
    
    def step(
        self,
        precip: np.ndarray,
        pet: np.ndarray,
        dt_hours: float = 1.0
    ) -> SimulationResults:
        """
        Run one timestep.
        
        Parameters
        ----------
        precip : ndarray
            Precipitation rate (mm/hr), shape (n_cells,)
        pet : ndarray
            Potential ET rate (mm/hr), shape (n_cells,)
        dt_hours : float
            Timestep in hours
            
        Returns
        -------
        SimulationResults
            Output fluxes for this timestep
        """
        if not self._initialized:
            self.initialize()
        
        precip = np.asarray(precip, dtype=np.float32)
        pet = np.asarray(pet, dtype=np.float32)
        
        # Run water balance
        fast, slow, sm, diag = core.crest.water_balance_grid(
            self.wb_params, self.wb_states, precip, pet, dt_hours
        )
        
        result = SimulationResults(
            fast_flow=fast,
            slow_flow=slow,
            soil_moisture=sm,
            diagnostics={
                'cells_processed': diag.cells_processed,
                'has_warnings': diag.has_warnings
            }
        )
        
        # Run routing if available
        if self.rt_states is not None:
            core.kinematic.reset_incoming_flows(self.rt_states)
            # TODO: Route water through network
            # This requires computing flows in topological order
        
        return result
    
    def run(
        self,
        precip_series: np.ndarray,
        pet_series: np.ndarray,
        dt_hours: float = 1.0,
        save_every: int = 1
    ) -> List[SimulationResults]:
        """
        Run simulation over multiple timesteps.
        
        Parameters
        ----------
        precip_series : ndarray
            Precipitation, shape (n_timesteps, n_cells)
        pet_series : ndarray
            PET, shape (n_timesteps, n_cells)
        dt_hours : float
            Timestep
        save_every : int
            Save results every N timesteps
            
        Returns
        -------
        list of SimulationResults
        """
        n_steps = len(precip_series)
        results = []
        
        for t in range(n_steps):
            result = self.step(precip_series[t], pet_series[t], dt_hours)
            
            if t % save_every == 0:
                results.append(result)
        
        return results
    
    def save_states(self) -> dict:
        """
        Save model states for restart.
        
        Returns
        -------
        dict
            State arrays that can be saved to file
        """
        states = {}
        
        # CREST states
        states['crest_soil_moisture'] = core.crest.get_states_soil_moisture(self.wb_states)
        
        # Kinematic states
        if self.rt_states is not None:
            states['kinematic_all'] = core.kinematic.get_all_states(self.rt_states)
        
        return states
    
    def load_states(self, states: dict):
        """
        Load model states from saved data.
        
        Parameters
        ----------
        states : dict
            State arrays from save_states()
        """
        if 'crest_soil_moisture' in states:
            core.crest.set_states_soil_moisture(
                self.wb_states, states['crest_soil_moisture']
            )
        
        if 'kinematic_all' in states and self.rt_states is not None:
            core.kinematic.set_all_states(
                self.rt_states, states['kinematic_all']
            )
        
        self._initialized = True
    
    def __repr__(self):
        return f"Simulation(model={self.model_name}, routing={self.routing_name}, cells={self.n_cells:,})"
