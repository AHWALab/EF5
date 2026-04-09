# Changelog

## EF5 Version [1.2.7]

### Fixed

- **Simple Inundation catchment formation now correctly uses `th_fim`**: Previously, `channelGridCell` flags were being set using the `th_fim` threshold *during* the downstream catchment-assignment walk, meaning nodes were traversed before all channel cells were identified. This caused incorrect catchment boundaries. The fix introduces a dedicated pre-pass (Pass 1) that evaluates `th_fim` and marks all channel cells first, so the subsequent downstream walk (Pass 2) operates on a fully resolved channel grid.

---

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
