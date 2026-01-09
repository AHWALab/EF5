"""
YAML configuration parser for ef5py.

Modern configuration format with clear structure:

Example YAML:
-------------
basic:
  dem: data/dem.tif
  fdir: data/fdir.tif
  fac: data/fac.tif
  projection: geographic

model:
  water_balance: CREST
  routing: kinematic
  
parameters:
  crest:
    wm: params/wm.tif  # or scalar: 150.0
    b: 0.4
    im: 0.05

forcing:
  precipitation:
    type: tif
    location: precip/
    pattern: "imerg.qpe.{timestamp}.tif"
    unit: mm/hr
    frequency: 30min
    
  pet:
    type: tif
    location: pet/
    pattern: "PET.{month:02d}.tif"
    unit: mm/day
    frequency: monthly

gauges:
  - name: outlet1
    lat: 22.57
    lon: -83.54
    observe_file: obs/outlet1.csv
    
  - name: outlet2
    cellx: 923
    celly: 313

simulation:
  start: 2024-01-01T00:00
  end: 2024-01-31T23:00
  timestep: 1h
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Union
from pathlib import Path
import yaml


@dataclass
class ForcingConfig:
    """Forcing data configuration."""
    type: str = "tif"
    location: str = ""
    pattern: str = ""
    unit: str = "mm/hr"
    frequency: str = "1h"


@dataclass  
class GaugeConfig:
    """Gauge/outlet configuration."""
    name: str = ""
    lat: Optional[float] = None
    lon: Optional[float] = None
    cellx: Optional[int] = None
    celly: Optional[int] = None
    observe_file: Optional[str] = None
    output_ts: bool = True


@dataclass
class Config:
    """Complete ef5py configuration."""
    # Basic grids
    dem: str = ""
    fdir: str = ""
    fac: str = ""
    projection: str = "geographic"
    esri_ddm: bool = True
    
    # Model selection
    water_balance_model: str = "CREST"
    routing_model: str = "kinematic"
    snow_model: Optional[str] = None
    
    # Parameters (can be scalars or paths to TIFs)
    crest_params: Dict[str, Union[float, str]] = field(default_factory=dict)
    kinematic_params: Dict[str, Union[float, str]] = field(default_factory=dict)
    
    # Forcing
    precipitation: Optional[ForcingConfig] = None
    pet: Optional[ForcingConfig] = None
    temperature: Optional[ForcingConfig] = None
    
    # Gauges
    gauges: List[GaugeConfig] = field(default_factory=list)
    
    # Simulation time
    start_time: Optional[str] = None
    end_time: Optional[str] = None
    timestep: str = "1h"
    
    # I/O
    output_dir: str = "output/"
    state_dir: Optional[str] = None


def load_yaml(path: str) -> Config:
    """
    Load configuration from YAML file.
    
    Parameters
    ----------
    path : str
        Path to YAML configuration file
        
    Returns
    -------
    Config
        Parsed configuration
    """
    with open(path, 'r') as f:
        data = yaml.safe_load(f)
    
    config = Config()
    
    # Parse basic section
    if 'basic' in data:
        basic = data['basic']
        config.dem = basic.get('dem', '')
        config.fdir = basic.get('fdir', basic.get('ddm', ''))
        config.fac = basic.get('fac', basic.get('fam', ''))
        config.projection = basic.get('projection', 'geographic')
        config.esri_ddm = basic.get('esri_ddm', True)
    
    # Parse model section
    if 'model' in data:
        model = data['model']
        config.water_balance_model = model.get('water_balance', 'CREST')
        config.routing_model = model.get('routing', 'kinematic')
        config.snow_model = model.get('snow')
    
    # Parse parameters
    if 'parameters' in data:
        params = data['parameters']
        config.crest_params = params.get('crest', {})
        config.kinematic_params = params.get('kinematic', {})
    
    # Parse forcing
    if 'forcing' in data:
        forcing = data['forcing']
        if 'precipitation' in forcing:
            p = forcing['precipitation']
            config.precipitation = ForcingConfig(
                type=p.get('type', 'tif'),
                location=p.get('location', ''),
                pattern=p.get('pattern', ''),
                unit=p.get('unit', 'mm/hr'),
                frequency=p.get('frequency', '1h')
            )
        if 'pet' in forcing:
            p = forcing['pet']
            config.pet = ForcingConfig(
                type=p.get('type', 'tif'),
                location=p.get('location', ''),
                pattern=p.get('pattern', ''),
                unit=p.get('unit', 'mm/day'),
                frequency=p.get('frequency', 'monthly')
            )
    
    # Parse gauges
    if 'gauges' in data:
        for g in data['gauges']:
            config.gauges.append(GaugeConfig(
                name=g.get('name', ''),
                lat=g.get('lat'),
                lon=g.get('lon'),
                cellx=g.get('cellx'),
                celly=g.get('celly'),
                observe_file=g.get('observe_file', g.get('obs')),
                output_ts=g.get('output_ts', True)
            ))
    
    # Parse simulation
    if 'simulation' in data:
        sim = data['simulation']
        config.start_time = sim.get('start')
        config.end_time = sim.get('end')
        config.timestep = sim.get('timestep', '1h')
        config.output_dir = sim.get('output_dir', 'output/')
        config.state_dir = sim.get('state_dir')
    
    return config


def save_yaml(config: Config, path: str):
    """Save configuration to YAML file."""
    data = {
        'basic': {
            'dem': config.dem,
            'fdir': config.fdir,
            'fac': config.fac,
            'projection': config.projection,
        },
        'model': {
            'water_balance': config.water_balance_model,
            'routing': config.routing_model,
        },
        'parameters': {
            'crest': config.crest_params,
            'kinematic': config.kinematic_params,
        },
        'gauges': [
            {'name': g.name, 'lat': g.lat, 'lon': g.lon}
            for g in config.gauges
        ],
        'simulation': {
            'start': config.start_time,
            'end': config.end_time,
            'timestep': config.timestep,
            'output_dir': config.output_dir,
        }
    }
    
    with open(path, 'w') as f:
        yaml.dump(data, f, default_flow_style=False)
