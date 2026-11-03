Ensemble Framework For Flash Flood Forecasting (EF5)
===
![version](https://img.shields.io/badge/version-1.3.0-teal.svg?style=flat) ![Linux Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Linux%20Build%20Status&branch=v1.2.5&style=flat&logo=linux) ![Windows Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Windows%20Build%20Status&branch=v1.2.5&style=flat&logo=windows)

EF5 was created by the Hydrometeorology and Remote Sensing Laboratory at the University of Oklahoma.
The goal of EF5 is to have a framework for distributed hydrologic modeling that is user friendly, adaptable, expandable, all while being suitable for large scale (e.g. continental scale) modeling of flash floods with rapid forecast updates. Currently EF5 incorporates 3 water balance models including the Sacramento Soil Moisture Accouning Model (SAC-SMA), Coupled Routing and Excess Storage (CREST), and hydrophobic (HP). These water balance models can be coupled with either linear reservoir or kinematic wave routing. 

## Learn More

General information about EF5 can be found at [AHWA Lab's Webpage](https://ahwa.lab.uiowa.edu/ensemble-framework-flash-flood-forecasting-ef5). YouTube videos with legacy basic training may be found at [EF5's YouTube Channel](https://www.youtube.com/channel/UCgoGJtdeqHgwoYIRhkgMwog). The source code for the original development transitioned to NWS operations can be found on HyDROSLab's GitHub at [https://github.com/HyDROSLab/EF5](https://github.com/HyDROSLab/EF5).

See [EF5's Documentation](https://ef5docs.readthedocs.io/en/latest/) for the EF5 operating manual which describes configuration options.

## Compiling

### Linux

Clone the source code from GitHub.   
1. autoreconf --force --install   
2. ./configure   
3. make   
   This compiles the EF5 application!

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

## Checking parameters and river channels

```
ef5 --check control.txt
```

This checks every simulation task in the `[Execute]` section without running
it. EF5 sets up the models as for a run (basin, gauges, parameter sets,
parameter grids and `th`) and looks at the parameter values the models will
use:

- **Cells with NaN or infinite parameters.** With `ROUTING=KW`, the discharge
  of every cell downstream of such a cell becomes NaN once the parameter is
  used (e.g. when it rains on it), and stays NaN. `maxq` shows these river
  stretches as 0. With `ROUTING=KW_PARALLEL` the flow is set to 0 instead, so
  the bad cells' water is lost without any sign in the outputs.
- **Parameter grids that hold their nodata value at modelled cells.** EF5
  multiplies the nodata value in as a number, and some parameters are then
  silently reset (e.g. a negative `wm` becomes 100). A grid that reads as
  nodata everywhere was most likely not read at all.

It writes two files to the task's `OUTPUT` folder:

| file | contents |
|---|---|
| `check_channels.tif` | one class per modelled cell: 1 hillslope, 2 channel, 3 channel cut, 4 bad parameters, 5 hillslope cut (water from a bad cell on its way to a channel) |
| `check_breaks.csv` | one row per break point (the first channel cell that water from bad cells reaches): location, FAC, whether the river is already cut further up, the bad parameters, how many bad cells feed it and where the largest one is, the channel cells below it, and the gauge below it |

Channel cells are those with FAC > `th`, as in the routing. Snow and
inundation parameters are not checked.

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

