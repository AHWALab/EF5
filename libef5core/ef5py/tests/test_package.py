"""
Test ef5py package structure and imports.
"""

import sys
from pathlib import Path

# Add build directory to path for _ef5core
build_dir = Path(__file__).parent.parent.parent / "build" / "ef5py"
sys.path.insert(0, str(build_dir))

# Add package directory  
pkg_dir = Path(__file__).parent.parent
sys.path.insert(0, str(pkg_dir))


def test_core_import():
    """Test that C++ core is importable."""
    import _ef5core as core
    print(f"✅ Core version: {core.__version__}")
    print(f"✅ Threads: {core.get_num_threads()}")
    assert hasattr(core, 'crest')
    assert hasattr(core, 'kinematic')


def test_config_parsers():
    """Test YAML and TXT config parsers."""
    from ef5py.config.yaml_parser import Config, load_yaml, ForcingConfig
    from ef5py.config.txt_parser import load_txt, parse_key_value_line
    
    # Test key-value parsing
    result = parse_key_value_line("lat=22.5 lon=-83.5 outputts=true")
    assert result['lat'] == 22.5
    assert result['lon'] == -83.5
    assert result['outputts'] is True
    print("✅ TXT parser key-value parsing works")
    
    # Test Config dataclass
    config = Config()
    config.dem = "test.tif"
    config.gauges = []
    assert config.dem == "test.tif"
    print("✅ Config dataclass works")


def test_grid_class():
    """Test Grid class with synthetic data."""
    import numpy as np
    from ef5py.grid import Grid, GridInfo
    
    # Create synthetic grid
    dem = np.random.rand(10, 15).astype(np.float32) * 100
    fdir = np.ones((10, 15), dtype=np.int32) * 4  # All flow south
    
    grid = Grid.from_arrays(dem, fdir, cell_size=0.01)
    
    assert grid.rows == 10
    assert grid.cols == 15
    assert grid.n_cells > 0
    print(f"✅ Grid: {grid}")


def test_simulation_class():
    """Test Simulation class."""
    import numpy as np
    from ef5py.grid import Grid
    from ef5py.simulation import Simulation
    
    # Create synthetic grid
    dem = np.random.rand(10, 15).astype(np.float32) * 100
    fdir = np.ones((10, 15), dtype=np.int32) * 4
    
    grid = Grid.from_arrays(dem, fdir, cell_size=0.01)
    
    # Create simulation
    sim = Simulation(grid, model="CREST", routing="kinematic")
    sim.set_parameters(wm=150.0, b=0.4, fc=1.5, iwu=0.5)
    
    # Run step
    precip = np.ones(grid.n_cells, dtype=np.float32) * 10.0
    pet = np.ones(grid.n_cells, dtype=np.float32) * 2.0
    
    result = sim.step(precip, pet, dt_hours=1.0)
    
    assert result.fast_flow is not None
    assert result.soil_moisture is not None
    print(f"✅ Simulation step worked!")
    print(f"   Fast flow mean: {result.fast_flow.mean():.4f}")
    print(f"   Soil moisture mean: {result.soil_moisture.mean():.1f}%")
    
    # Test state save/load
    states = sim.save_states()
    assert 'crest_soil_moisture' in states
    print(f"✅ State save/load works")


if __name__ == "__main__":
    print("=" * 50)
    print("Testing ef5py package")
    print("=" * 50)
    
    test_core_import()
    test_config_parsers()
    test_grid_class()
    test_simulation_class()
    
    print("\n" + "=" * 50)
    print("✅ ALL TESTS PASSED!")
    print("=" * 50)
