#!/usr/bin/env python3
"""Test EF5 Python bindings"""
import sys
sys.path.insert(0, 'build/ef5py')
import _ef5core as ef5
import numpy as np

print(f"Version: {ef5.__version__}")
print(f"Threads: {ef5.get_num_threads()}")

# Test CREST water balance
n = 1000
params = ef5.crest.make_params(n)
states = ef5.crest.make_states(n)
ef5.crest.initialize_states(params, states)

precip = np.ones(n, dtype=np.float32) * 10.0
pet = np.ones(n, dtype=np.float32) * 2.0
fast, slow, sm, diag = ef5.crest.water_balance_grid(params, states, precip, pet, 1.0)

print(f"\nWater balance: {diag.cells_processed} cells, warnings={diag.has_warnings()}")
print(f"Fast flow mean: {fast.mean():.6f}")
print(f"Soil moisture mean: {sm.mean():.1f}%")

# Test state access
sm_out = ef5.crest.get_states_soil_moisture(states)
print(f"State extraction: {len(sm_out)} values")

# Test kinematic
kw_params = ef5.kinematic.make_params(n)
kw_states = ef5.kinematic.make_states(n)
ef5.kinematic.initialize_states(kw_params, kw_states)
print(f"\nKinematic: {len(kw_states)} cells initialized")

print("\n✅ ALL TESTS PASSED!")
