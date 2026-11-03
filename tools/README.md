# EF5 tools

## ef5_check.py: find bad parameters and broken river channels

A NaN in a parameter grid makes EF5 compute NaN for that cell:

- **`ROUTING=KW`:** the NaN flows downstream and the river is cut. Discharge
  is NaN below that point, and `maxq` shows 0 there, because NaN never wins a
  `>` comparison.
- **`ROUTING=KW_PARALLEL`:** the bad cell's flow is set to 0 instead. Upstream
  water passes through, but that cell's own runoff disappears without any
  sign in the outputs.

Grid nodata values (e.g. -9999) are not treated as missing either. EF5
multiplies them in, and then sometimes silently replaces the result with a
default.

`ef5_check.py` finds these problems from the control file. You don't need to
overlay FAC and `maxq` in a GIS.

EF5 itself has a similar check, `ef5 --check control.txt` (see the main
README). It looks at the parameter values as EF5 read them, so it also shows
grids that EF5 could not read. This tool reads the grids with GDAL, so it
shows what is in the files. It also checks FAC against the flow directions
and finds breaks in a finished run.

```
python tools/ef5_check.py control.txt                     # inputs only, no run needed
python tools/ef5_check.py control.txt --output outputs/   # also find breaks in a finished run
```

Run it from the folder you run EF5 in (control-file paths are relative to
it), or pass `--run-dir`. It needs Python 3 with NumPy and GDAL.

### What it checks

**Inputs (always):**

- **Every water balance and routing parameter, at every modelled cell.**
  The value is computed the way EF5 computes it: the parameter-set scalar
  times the grid value, with EF5's rules applied (a grid value of 0 is used
  as 0.01; the resets in `CRESTModel.cpp` and `KinematicRoute.cpp`). It
  reports:
  - `NaN`: EF5 will compute NaN at this cell.
  - `nodata`: the grid's nodata value was used as a number. The CSV shows
    what EF5 ended up with.
  - `outside grid`: the cell is outside the parameter grid, so EF5 uses the
    scalar alone.
  - `reset`: the value was out of range, and EF5 silently replaced it (for
    example a negative `beta` becomes 0.6, a NaN CREST `b` becomes 0).
  - `zero`: the grid value 0 was replaced by 0.01 (summary only, unless
    `--all` is given).

  `alpha` and `beta` are only used on channel cells, so on hillslope cells
  they are counted but not listed.
- **Grid alignment.** A parameter grid with the DEM's size but a different
  extent is used cell by cell anyway.
- **FAC against flow directions.** It reports FAC decreasing downstream, and
  gauges with more upstream cells than FAC allows (EF5 stops walking there
  and leaves the rest out).
- **Rivers at risk.** For each NaN cell, it reports the first channel cell
  (FAC > `th`) its water reaches.

**A finished run (`--output` or `--maxq`):**

- **Breaks.** These are channel cells that receive flow from upstream but
  never had any flow themselves in `maxq`. Each break gets a cause: a NaN
  parameter at the cell itself, or NaN arriving from another branch (with
  the location of the NaN cell it traced up to).
- **Modelled cells.** It checks that its modelled cells match the run's.

### Output

The summary goes to the console. These CSV files go next to `--prefix`
(default `ef5_check`). Each has one row per cell, with the lat/lon of the cell
centre, so they open in QGIS as point layers:

| file | contents |
|---|---|
| `_gauges.csv` | where each gauge ends up (after `basinarea` snapping), and whether it is used |
| `_params.csv` | every parameter problem: cell, parameter, issue, grid value, value EF5 uses |
| `_risk.csv` | channel cells where NaN water enters a river |
| `_breaks.csv` | breaks in the run, with their cause (only with `--output`) |

### How it decides which cells are modelled

It follows `CarveBasin` in `src/BasicGrids.cpp`:

1. Gauges are placed by `cellx`/`celly` or by `lat`/`lon`.
2. A gauge with `basinarea` is moved to the cell within 20 km whose FAC best
   matches that area.
3. Starting from the gauges, EF5 walks upstream, outlets (largest FAC)
   first.
4. Each cell uses the parameter set of the nearest gauge downstream. A gauge
   without its own set inherits from the gauge below it, and an outlet
   without one uses `defaultparamsgauge`.

Placing gauges by `lat`/`lon` or `basinarea` is supported for
`PROJ=geographic` only.

### Limitations

- `--output` reads `maxq` from GeoTIFF outputs (`qpf_maxq` if present, else
  `maxq`). For Zarr output, pass a GeoTIFF with `--maxq`.
- Parameters are checked for the water balance and routing models. Snow and
  inundation parameters are not checked.
- `maxq` is written rounded to 0.1 m³/s, so a break needs at least
  `--min-flow` (default 0.1) upstream.
