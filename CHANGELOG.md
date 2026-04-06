# Changelog

## EF5 Version [1.2.8]

### Added

- New `runoff` gridded output for distributed simulations, written as GeoTIFF output grids
- New `subrunoff` gridded output for distributed simulations, written as GeoTIFF output grids

### Changed

- Updated EF5 version metadata to `1.2.8`

## EF5 Version [1.2.6]

### Added

- New parameter `th_fim` to set a threshold for Simple Inundation simulations
- New output grid for simple inundation: HAND catchments
- Resource utilization summary printed after any task completes

### Fixed

- Incorrect precipitation values when switching from Normal to Long Range simulations

### Optimized

- Reduced duplicate warning prints, especially for "Node Soil Moisture" messages

---

_For previous versions, please refer to the git history._
