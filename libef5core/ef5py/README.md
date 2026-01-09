# ef5py

Python interface to EF5 distributed hydrological model.

## Installation

```bash
# Build the C++ core first
cd libef5core
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4

# Install Python package
cd ../ef5py
pip install -e ".[full]"
```

## Quick Start

```python
import ef5py

# Load from config file
config = ef5py.load_config("simulation.yaml")

# Or build programmatically
grid = ef5py.Grid.from_tifs("dem.tif", "fdir.tif", "fac.tif")
sim = ef5py.Simulation(grid, model="CREST", routing="kinematic")

# Set parameters
sim.set_parameters(wm=150.0, b=0.4, fc=1.5, iwu=0.5)

# Run timestep
import numpy as np
precip = np.ones(grid.n_cells, dtype=np.float32) * 10.0
pet = np.ones(grid.n_cells, dtype=np.float32) * 2.0
result = sim.step(precip, pet, dt_hours=1.0)

print(f"Soil moisture: {result.soil_moisture.mean():.1f}%")
```

## Configuration Formats

### YAML (recommended)

```yaml
basic:
  dem: data/dem.tif
  fdir: data/fdir.tif
  fac: data/fac.tif

model:
  water_balance: CREST
  routing: kinematic

parameters:
  crest:
    wm: 150.0
    b: 0.4

gauges:
  - name: outlet
    lat: 22.57
    lon: -83.54
```

### Legacy TXT (backward compatible)

```ini
[Basic]
DEM=data/dem.tif
DDM=data/fdir.tif
FAM=data/fac.tif

[CREST]
WM=150.0
B=0.4
```

## Features

- **High performance**: C++ core with OpenMP parallelization
- **CREST water balance**: Full implementation with diagnostics
- **Kinematic wave routing**: Multi-threaded wavefront algorithm
- **State management**: Save/load for warm starts and data assimilation
- **Multiple config formats**: YAML (modern) and TXT (legacy)
