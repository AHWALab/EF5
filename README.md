Ensemble Framework For Flash Flood Forecasting (EF5)
===
![version](https://img.shields.io/badge/version-1.3.0-teal.svg?style=flat) ![Linux Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Linux%20Build%20Status&branch=master&style=flat&logo=linux) ![Windows Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Windows%20Build%20Status&branch=master&style=flat&logo=windows)

EF5 was created by the Hydrometeorology and Remote Sensing Laboratory at the University of Oklahoma.
The goal of EF5 is to have a framework for distributed hydrologic modeling that is user friendly, adaptable, expandable, all while being suitable for large scale (e.g. continental scale) modeling of flash floods with rapid forecast updates. Currently EF5 incorporates 3 water balance models including the Sacramento Soil Moisture Accouning Model (SAC-SMA), Coupled Routing and Excess Storage (CREST), and hydrophobic (HP). These water balance models can be coupled with linear reservoir or kinematic wave routing; the kinematic wave can also run on several CPU cores (`ROUTING=KW_PARALLEL`).

## What's new in 1.3

- `ROUTING=KW_PARALLEL`: the kinematic wave router on several CPU cores (OpenMP; set `OMP_NUM_THREADS`). It uses the `KW` parameter sets and state files.
- `OUTPUT_TYPE=ZARR`: all gridded outputs of a run in one Zarr v3 store instead of one GeoTIFF per grid and step (see [Zarr output](#zarr-output)).
- `runoff` and `subrunoff` output grids: surface and subsurface runoff from the water balance, in mm/h.
- With a forecast period (`TIME_BEGIN_LR`), maximum grids for the observed period (`qpe_max*`) and for the whole run (`qpf_max*`).
- DREAM output has a column for every objective function (`nsce`, `cc`, `sse`, `mkge`).
- Parameter grids must be stored as one image plane: a grid stored as separate planes stops the task, with the command that fixes it. EF5 exits with status 1 when a task did not run.

See [CHANGELOG.md](CHANGELOG.md) for details and upgrade notes.

## Learn More

General information about EF5 can be found at [AHWA Lab's Webpage](https://ahwa.lab.uiowa.edu/ensemble-framework-flash-flood-forecasting-ef5). YouTube videos with legacy basic training may be found at [EF5's YouTube Channel](https://www.youtube.com/channel/UCgoGJtdeqHgwoYIRhkgMwog). The source code for the original development transitioned to NWS operations can be found on HyDROSLab's GitHub at [https://github.com/HyDROSLab/EF5](https://github.com/HyDROSLab/EF5).

See [EF5's Documentation](https://ef5docs.readthedocs.io/en/latest/) for the EF5 operating manual which describes configuration options.

## Compiling

### Requirements

- A C++ compiler, autoconf, automake and pkg-config
- libtiff, libgeotiff and zlib (development files)
- OpenMP (comes with GCC), for `ROUTING=KW_PARALLEL` and parallel water balance
- Optional, for Zarr output: GDAL 3.11 or newer (development files)

On Ubuntu 26.04 or newer:

    sudo apt-get install build-essential autoconf automake pkg-config libgeotiff-dev libtiff-dev zlib1g-dev libgdal-dev

Ubuntu 24.04, Debian 13 and other older systems have a GDAL older than 3.11; EF5 then builds without Zarr output. To get Zarr output there, install a newer GDAL (for example with conda: `conda install -c conda-forge "gdal>=3.11"`) and point `PKG_CONFIG_PATH` at it.

### Linux

Clone the source code from GitHub.   
1. autoreconf --force --install   
2. ./configure   
3. make   
   This compiles the EF5 application (`bin/ef5`)!

The last line printed by `./configure` says whether Zarr output is built (`Zarr output: yes`). It is built when GDAL 3.11 or newer is found. Use `./configure --with-zarr` to make a missing GDAL an error, or `./configure --without-zarr` to build without GDAL.

The script `compile_ef5.sh` installs the dependencies with the system package manager and runs these steps.

### OS X

Clone the source code from GitHub. Use the EF5 Xcode project found in the EF5 folder and compile the project.

### Windows

Currently cross-compiling from Linux is the recommended way of generating Windows binaries.

Clone the source code from GitHub.

1. autoreconf --force --install
2. For 32-bit Windows installations use ./configure --host=i686-w64-mingw32   
   For 64-bit Windows installations use ./configure --host=x86_64-w64-mingw32

3. make   
   This compiles the EF5 application!

Windows builds do not include Zarr output.

## Zarr output

With `OUTPUT_TYPE=ZARR` in a task, the gridded outputs listed in `OUTPUT_GRIDS` are written to one Zarr v3 store, `<OUTPUT>/<YYYYMMDD_HHUU>.zarr`. Its name is the end time of the run, or the start of the forecast (`TIME_BEGIN_LR`) when one is set. An existing store with that name is replaced.

- Per-step grids are arrays `(time, y, x)`: `q`, `sm`, `rp`, `precip`, `pet`, `runoff`, `subrunoff`, `swe`, `temp`, `depth`, `unitq`, `thres`.
- Maximum and accumulated grids are arrays `(y, x)`: `maxq`, `maxsm`, ... or, with a forecast, `qpe_maxq`, `qpf_maxq`, ..., and `qpeaccum`, `qpfaccum`, `handcatchment`.
- `time` is in seconds since 1970-01-01 UTC; `forcing_phase` is 0 for QPE steps and 1 for forecast steps. `y` and `x` are cell centres. Nodata is -9999.

Read it with xarray and zarr-python 3 (zarr-python 2 cannot read Zarr v3):

```python
import xarray as xr
ds = xr.open_zarr("outputs/20230610_0000.zarr")
ds["q"].sel(time="2023-06-05T12:00").plot()
```

## Parameter grids

Parameter grids must be GeoTIFFs stored as one image plane. A grid stored as separate planes (`PlanarConfiguration=2`) stops the task with an error naming the file. Rewrite it with:

    gdal_translate -co INTERLEAVE=PIXEL in.tif out.tif

## Contributors

The following people currently contribute to EF5:

- [Humberto Vergara](https://ahwa.lab.uiowa.edu/people/humberto-vergara-phd)
- [Naman Mehta](https://ahwa.lab.uiowa.edu/people/naman-mehta)
- [Vanessa Robledo](https://ahwa.lab.uiowa.edu/people/vanessa-robledo-msc)
- [Santiago Henao Gomez](https://ahwa.lab.uiowa.edu/people/santiago-henao-gomez)
- [Mohamed Abdelkader](https://ahwa.lab.uiowa.edu/people/mohamed-abdelkader)

### Original contributors

The following people are acknowledged for their contributions to the creation of EF5.

- Zac Flamig
- Humberto Vergara
- Race Clark
- JJ Gourley
- Yang Hong

