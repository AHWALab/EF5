# Changelog

## EF5 Version [1.3.1]

Version 1.3.1 tells you how many CPU cores EF5 runs on. Outputs are the same
as 1.3.0.

### Added

- Each simulation and calibration task logs the number of OpenMP threads it
  uses: `Using 16 OpenMP threads (set OMP_NUM_THREADS to change)`.
- With `ROUTING=KW_PARALLEL` on one thread, EF5 prints a warning. HPC job
  environments often set `OMP_NUM_THREADS=1`, and `KW_PARALLEL` then runs on
  one core without saying so.
- README section "Using several cores": how to set `OMP_NUM_THREADS` in SGE
  and Slurm job scripts and with Apptainer.

### Changed

- Version 1.3.1; container image `ghcr.io/ahwalab/ef5:1.3.1`.
- The README no longer says the water balance runs in parallel: only
  `KW_PARALLEL` routing, reading precipitation grids and DREAM calibration do.

## EF5 Version [1.3.0]

Version 1.3.0 adds a parallel kinematic wave router, Zarr output, runoff output
grids, separate QPE/QPF maximum grids and all-metric DREAM output. It also
includes the runoff grids prepared for 1.2.8, which was never released.

### Upgrade notes

Read these before replacing an older EF5 in an operational setup:

- **Parameter grids must be stored as one image plane.** A parameter grid
  (water balance, routing, snow or inundation) stored as separate planes
  (TIFF `PlanarConfiguration=2`) now stops the task with an error. Older
  versions read such grids as all nodata, without a message, and used the
  parameter set's values instead of the grid. Rewrite these grids with
  `gdal_translate -co INTERLEAVE=PIXEL in.tif out.tif`.
- **EF5 exits with status 1 when a task does not run.** Scripts that check the
  exit status now see failed tasks.
- **Maximum grids are named by period when a forecast is used.** With
  `TIME_BEGIN_LR` set, the maximum grids are written twice: `qpe_max*.tif` at
  the switch to the forecast, and `qpf_max*.tif` at the end of the run (over the
  whole run). Without a forecast period they keep their old names (`maxq.*.tif`
  and so on).
- **Zarr output is Zarr format v3** and needs EF5 built with GDAL 3.11 or newer.
  Read it with zarr-python 3 (`zarr>=3`) or xarray with zarr-python 3;
  zarr-python 2 cannot read Zarr v3.

### Added

- **`ROUTING=KW_PARALLEL`**: a kinematic wave router that runs on several CPU
  cores with OpenMP (set the number with `OMP_NUM_THREADS`). Cells are routed in
  waves from the headwaters down: a cell is routed only after every cell
  upstream of it, and the cells of one wave are routed in parallel. Results
  match `KW` to output precision and do not depend on the number of threads.
  - It uses the same parameters as `KW`: a task with `ROUTING=KW_PARALLEL` can
    name `[KWParamSet]` and `[KWCaliParams]` sections.
  - It reads and writes the same state files as `KW` (`kwr_*.tif`), so a run
    can switch between the two routers.
  - It differs from `KW` where a cell's flow is NaN (for example because of a
    NaN parameter): `KW` passes the NaN on downstream, `KW_PARALLEL` sets it to
    0.
- **Zarr output (`OUTPUT_TYPE=ZARR`)**: instead of one GeoTIFF per grid and
  time step, all gridded outputs of a run go to one Zarr v3 store,
  `<OUTPUT>/<YYYYMMDD_HHUU>.zarr`. The name is the end time of the run, or the
  start of the forecast (`TIME_BEGIN_LR`) when one is set. An existing store
  with that name is replaced.
  - Per-step grids are 3-D arrays `(time, y, x)` named after the output (`q`,
    `sm`, `rp`, `precip`, `pet`, `swe`, `temp`, `depth`, `unitq`, `thres`,
    `runoff`, `subrunoff`), in chunks of 1 x 512 x 512.
  - Maximum and accumulated grids are 2-D arrays `(y, x)`, such as `maxq` or,
    with a forecast, `qpe_maxq` and `qpf_maxq`, plus `qpeaccum`, `qpfaccum` and
    `handcatchment`.
  - Coordinates: `time` (seconds since 1970-01-01 00:00:00 UTC), `y` and `x`
    (cell centres from the DEM), and `forcing_phase` along `time`
    (0 = QPE step, 1 = QPF/long-range step).
  - Arrays are float32 with nodata -9999 (the Zarr `fill_value`, and the CF
    attribute `missing_value`, so that xarray reads nodata cells as NaN) and
    the DEM's coordinate reference system. `forcing_phase` is int16 with fill
    value -1 (a step not written). They are compressed with Blosc/LZ4 when the GDAL build supports it,
    otherwise ZSTD, otherwise GZIP.
  - `OUTPUT_TYPE=GEOTIFF` (the default) writes GeoTIFF as before.
- **`runoff` and `subrunoff` output grids** (`OUTPUT_GRIDS=...|RUNOFF|SUBRUNOFF`):
  the surface (fast) and subsurface (slow) runoff that the water balance model
  produces in each cell and time step, in mm/h, before routing. They are written
  after the warm-up, like the other per-step grids, as
  `runoff.<time>.<model>.tif` and `subrunoff.<time>.<model>.tif`, or as the
  `runoff` and `subrunoff` arrays of the Zarr store.
- **QPE and QPF maximum grids**: with a forecast period (`TIME_BEGIN_LR`), the
  maximum grids (`maxq`, `maxsm`, `maxrp`, `maxunitq`, `maxswe`, `maxdepth`) are
  written once for the observed (QPE) period, with the prefix `qpe_`, and once
  for the whole run including the forecast, with the prefix `qpf_`.
- **All metric scores in DREAM output**: besides the objective being calibrated,
  the DREAM output file has one column for each objective function (`nsce`, `cc`,
  `sse`, `mkge`) for every parameter sample, so samples can be compared on all
  metrics without re-running them.
- **Separate-plane check for parameter grids**: before reading a parameter
  grid, EF5 checks how the TIFF is stored. Every parameter grid of the task that
  is stored as separate planes is reported, with the `gdal_translate` command
  that fixes it, before the task stops.

### Changed

- A task that cannot start (for example a missing grid, or a parameter grid
  stored as separate planes) now stops in every run style. `SIMU_RP`,
  `CALI_ARS`, `CALI_DREAM`, the DREAM ensemble and `BASIN_AVG` used to carry on
  after a failed start. If any task did not run, EF5 says so at the end and
  exits with status 1.
- A task with `OUTPUT_TYPE=ZARR` on an EF5 built without Zarr output stops at
  start-up, before reading any data, and says how to fix it.
- The start-up banner shows version 1.3.0 in green.

### Build

- GDAL is needed only for Zarr output, and only GDAL 3.11 or newer is used for
  it. Older GDAL versions write a draft of Zarr v3 that zarr-python and xarray
  cannot read: GDAL 3.6 uses the old `meta/` and `data/` layout, and GDAL 3.8
  and 3.10 leave out the required `bytes` codec.
  - `./configure` builds Zarr output when it finds GDAL 3.11 or newer through
    pkg-config. Otherwise it builds EF5 without Zarr output and prints a warning.
    The last line of its output says which (`Zarr output: yes` or `no`).
  - `./configure --with-zarr` makes a missing GDAL 3.11 an error, and
    `./configure --without-zarr` skips GDAL altogether.
- The GitHub Actions Linux build runs in an Ubuntu 26.04 container (GDAL 3.12),
  so the released Linux binary has Zarr output. The Windows builds are built
  without Zarr output.
- **Container images.** `docker/` has a Dockerfile and an Apptainer recipe
  (`ef5.def`), both building EF5 on Ubuntu 26.04 with Zarr output.
  - `docker/build.sh` builds either image, and `docker/ef5-docker` runs the
    Docker image like a normal `ef5` binary.
  - `docker/compose.yaml` is a Docker Compose service to copy into your own
    setup.
  - Each release publishes the image as `ghcr.io/ahwalab/ef5:<version>` and
    `:latest`. HPC users can run it with
    `apptainer pull ef5.sif docker://ghcr.io/ahwalab/ef5:<version>`.
  - See `docker/README.md`.
- The README explains how to compile without root on HPC clusters (libraries
  from conda-forge or modules).
- GitHub releases take their notes from this changelog.

---

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
