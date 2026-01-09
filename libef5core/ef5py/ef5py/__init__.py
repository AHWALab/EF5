"""
ef5py - Python interface to EF5 Hydrological Model

A modern Python package for distributed hydrological modeling,
providing access to the C++ computational core of EF5.

Example
-------
>>> import ef5py
>>> grid = ef5py.Grid.from_tifs(dem="dem.tif", fdir="fdir.tif", fac="fac.tif")
>>> sim = ef5py.Simulation(grid, model="CREST", routing="KW")
>>> results = sim.run(precip, pet, step_hours=1.0)
"""

__version__ = "2.0.0"

# Import C++ core
try:
    from . import _ef5core as core
except ImportError:
    import _ef5core as core

# Re-export core modules for convenience
crest = core.crest
kinematic = core.kinematic
GridCell = core.GridCell
RoutingTopology = core.RoutingTopology

# High-level API imports
from .grid import Grid
from .simulation import Simulation
from .config import load_config

# Control threads
def set_threads(n: int):
    """Set number of OpenMP threads for parallel computation."""
    core.set_num_threads(n)

def get_threads() -> int:
    """Get number of OpenMP threads."""
    return core.get_num_threads()

__all__ = [
    "Grid",
    "Simulation", 
    "load_config",
    "crest",
    "kinematic",
    "GridCell",
    "RoutingTopology",
    "set_threads",
    "get_threads",
    "__version__",
]
