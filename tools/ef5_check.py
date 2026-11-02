#!/usr/bin/env python3
"""Check an EF5 setup for bad parameters and broken river channels.

    python tools/ef5_check.py control.txt [--task NAME] [--output DIR]

Without --output, only the inputs are checked (no EF5 run needed):

  * Every water balance and routing parameter is computed per cell the way
    EF5 computes it (scalar from the parameter set x grid value, EF5's 0 ->
    0.01 rule and its clamps), and problems are reported: NaN (EF5 will
    produce NaN there), grid nodata used as a value, values EF5 silently
    resets, zeros, and cells outside the grid.
  * Grids whose size matches the DEM but whose extent does not (EF5 then
    uses them cell by cell anyway).
  * FAC that disagrees with the flow directions (FAC decreasing downstream,
    or fewer cells upstream of a gauge than EF5 will walk).
  * Channel cells at risk: where water from a NaN cell reaches a river.

With --output (the OUTPUT folder of a finished run), it also finds the actual
breaks: channel cells (FAC > th) that receive flow from upstream but never
had any flow themselves in maxq, and traces each one to its cause.

Results go to the console and to CSV files (one row per cell, with lat/lon of
the cell centre) that open in QGIS as point layers:
  <prefix>_params.csv   parameter problems
  <prefix>_risk.csv     channel cells where NaN water enters a river
  <prefix>_breaks.csv   actual breaks and their causes (with --output)

Paths in the control file are relative to the folder EF5 runs in; use
--run-dir if that is not the current folder. Only the geographic projection
is supported for placing gauges; with PROJ=laea, pass --output so the
modelled cells are taken from the run.
"""
import argparse
import csv
import glob
import math
import os
import sys
from collections import OrderedDict, deque

import numpy as np
from osgeo import gdal

gdal.UseExceptions()

# Parameter order as in src/Models.tbl
MODEL_PARAMS = {
    "hp": ["precip", "split"],
    "crest": ["wm", "b", "im", "ke", "fc", "iwu"],
    "hymod": ["huz", "b", "alp", "nq", "kq", "ks", "xcuz", "xq", "xs", "precip"],
    "sac": ["uztwm", "uzfwm", "uzk", "pctim", "adimp", "riva", "zperc", "rexp",
            "lztwm", "lzfsm", "lzfpm", "lzsk", "lzpk", "pfree", "side", "rserv",
            "uztwc", "uzfwc", "adimc", "lztwc", "lzfsc", "lzfpc"],
}
ROUTE_PARAMS = {
    "lr": ["coem", "river", "under", "leako", "leaki", "th", "iso", "isu"],
    "kw": ["under", "leaki", "th", "isu", "alpha", "beta", "alpha0"],
    "kw_parallel": ["under", "leaki", "th", "isu", "alpha", "beta", "alpha0"],
}
# Routing parameters that are only used on channel cells
CHANNEL_ONLY = {"alpha", "beta"}

# EF5 flow directions (src/Defines.h) as (row, col) steps
FLOW_STEP = {1: (0, 1), 2: (-1, 1), 3: (-1, 0), 4: (-1, -1),
             5: (0, -1), 6: (1, -1), 7: (1, 0), 8: (1, 1)}
ESRI_TO_EF5 = {64: 3, 128: 2, 1: 1, 2: 8, 4: 7, 8: 6, 16: 5, 32: 4}


# ---------------------------------------------------------------- control file

def parse_control(path):
    """Parse an EF5 control file the way src/Config.cpp does.

    Returns a list of (type, name, [(key, value), ...]) with type, name and
    keys lower-cased. Comments are //, /* */ and #, but only where a key or
    section can start; a value ends at whitespace.
    """
    text = open(path, newline="").read()
    sections, i, n = [], 0, len(text)
    current = None
    while i < n:
        ch = text[i]
        if text.startswith("//", i) or ch == "#":
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2)
            i = n if i < 0 else i + 2
        elif ch in " \t\r\n":
            i += 1
        elif ch == "[":
            j = text.index("]", i)
            head = text[i + 1:j].split(None, 1)
            current = (head[0].lower(), head[1].strip().lower() if len(head) > 1 else "", [])
            sections.append(current)
            i = j + 1
        else:
            j = text.find("=", i)
            key = text[i:j].strip().lower()
            k = j + 1
            while k < n and text[k] not in " \r\n":
                k += 1
            if current is not None:
                current[2].append((key, text[j + 1:k]))
            i = k
    return sections


def first(pairs, key, default=None):
    for k, v in pairs:
        if k == key:
            return v
    return default


def is_true(value):
    return value is not None and value.lower() in ("true", "yes")


# ------------------------------------------------------------------- grids

class Grid:
    def __init__(self, path):
        ds = gdal.Open(path)
        band = ds.GetRasterBand(1)
        self.path = path
        self.data = band.ReadAsArray().astype(np.float32)
        self.nodata = band.GetNoDataValue()
        self.gt = ds.GetGeoTransform()
        self.rows, self.cols = self.data.shape
        # EF5 keeps the extent and cell size as read from the GeoTIFF tags
        self.left = np.float32(self.gt[0])
        self.top = np.float32(self.gt[3])
        self.cell = float(self.gt[1])

    def is_nodata(self, values):
        if self.nodata is None or math.isnan(self.nodata):
            return np.zeros(values.shape, bool)
        return values == np.float32(self.nodata)

    def loc(self, lon, lat):
        """Grid::GetGridLoc: cell containing (lon, lat), clamped to the grid;
        inside is False where EF5 skips the lookup."""
        lon = np.asarray(lon, np.float32)
        lat = np.asarray(lat, np.float32)
        x = ((lon - self.left) / self.cell).astype(np.int64)
        y = ((self.top - lat) / self.cell).astype(np.int64)
        right = self.left + np.float32(self.cell * self.cols)
        bottom = self.top - np.float32(self.cell * self.rows)
        inside = (lon >= self.left) & (lon <= right) & (lat >= bottom) & (lat <= self.top)
        return np.clip(y, 0, self.rows - 1), np.clip(x, 0, self.cols - 1), inside


def cell_centre(grid, rows, cols):
    lon = grid.gt[0] + (np.asarray(cols) + 0.5) * grid.gt[1]
    lat = grid.gt[3] + (np.asarray(rows) + 0.5) * grid.gt[5]
    return lat, lon


# ------------------------------------------------------------- the basin

class Basin:
    """The cells EF5 models, their flow links and their parameter gauges."""

    def __init__(self, dem, ddm, fam, esri, self_fam):
        self.dem, self.fam = dem, fam
        self.rows, self.cols = dem.rows, dem.cols
        raw = ddm.data
        bad = (ddm.is_nodata(raw) | ~np.isfinite(raw) | dem.is_nodata(dem.data)
               | fam.is_nodata(fam.data))
        d = np.zeros(raw.shape, np.int16)
        if esri:
            for code, val in ESRI_TO_EF5.items():
                d[raw == code] = val
        else:
            for val in FLOW_STEP:
                d[raw == val] = val
        bad |= d == 0                              # 0 means nodata to EF5 too
        d[bad] = 0
        self.valid = ~bad                          # FixFAM: nodata in any grid
        self.self_fam = self_fam
        # Linear index of the downstream cell, or -1
        r, c = np.indices(d.shape)
        dr = np.zeros(d.shape, np.int64)
        dc = np.zeros(d.shape, np.int64)
        for code, (sr, sc) in FLOW_STEP.items():
            dr[d == code], dc[d == code] = sr, sc
        nr, nc = r + dr, c + dc
        ok = (d > 0) & (nr >= 0) & (nr < self.rows) & (nc >= 0) & (nc < self.cols)
        down = np.where(ok, nr * self.cols + nc, -1).ravel()
        # A cell is only upstream of its neighbour if that neighbour is valid
        down[(down >= 0) & ~self.valid.ravel()[np.maximum(down, 0)]] = -1
        self.down = down
        order = np.argsort(down, kind="stable")
        self.up_cells = order[down[order] >= 0]
        self.up_start = np.searchsorted(down[self.up_cells], np.arange(down.size))
        self.up_end = np.searchsorted(down[self.up_cells], np.arange(down.size), side="right")

    def upstream(self, idx):
        return self.up_cells[self.up_start[idx]:self.up_end[idx]]

    def carve(self, gauges, warn):
        """BasicGrids.cpp CarveBasin: walk upstream from the gauges.

        gauges: list of dicts with 'name', 'index', 'fac', 'continue'.
        Sets self.model (bool, flat) and self.label (index into gauges of the
        nearest downstream gauge, flat) and returns the parent gauge of each
        gauge (None for independent basins).
        """
        n = self.rows * self.cols
        self.model = np.zeros(n, bool)
        self.label = np.full(n, -1, np.int64)
        at = {}
        for gi, g in enumerate(gauges):
            if g["index"] in at:
                warn(f"Gauges {gauges[at[g['index']]]['name']} and {g['name']} are in the same cell; "
                     "EF5 keeps only one of them")
                g["dropped"] = True
                continue
            at[g["index"]] = gi
        order = sorted((gi for gi, g in enumerate(gauges) if not g.get("dropped")),
                       key=lambda gi: -gauges[gi]["fac"])
        parent = {}
        used = set()
        for start in order:
            if start in used:
                continue
            parent[start] = None
            queue = deque([(gauges[start]["index"], start)])
            while queue:
                idx, lab = queue.popleft()
                gi = at.get(idx)
                keep = True
                if gi is not None:
                    if gi != lab:
                        parent[gi] = lab
                    lab = gi
                    used.add(gi)
                    keep = gauges[gi]["continue"]
                self.model[idx] = True
                self.label[idx] = lab
                if keep:
                    for u in self.upstream(idx):
                        if not self.model[u]:
                            queue.append((u, lab))
        return parent


# -------------------------------------------------------------- parameters

def param_set_values(section_pairs, names):
    """Scalars per gauge and grid paths from a parameter set section."""
    per_gauge, grids, cur = OrderedDict(), {}, None
    for k, v in section_pairs:
        if k == "gauge":
            cur = v.lower()
            per_gauge[cur] = {}
        elif k.endswith("_grid") and k[:-5] in names:
            grids[k[:-5]] = v
        elif cur is not None and k in names:
            per_gauge[cur][k] = float(v)
    return per_gauge, grids


def cell_params(basin, gauges, parent, default, per_gauge, grids, names, run_dir,
                model, problems, warn):
    """Final value of every parameter at every modelled cell, as EF5 computes
    it. Records problems; returns {name: float32 array over modelled cells}."""
    cells = np.nonzero(basin.model)[0]
    rows, cols = np.divmod(cells, basin.cols)
    # Parameter set of each gauge: its own, else its parent's, else default
    sets = {}
    def set_of(gi):
        if gi in sets:
            return sets[gi]
        own = per_gauge.get(gauges[gi]["name"])
        if own is None:
            own = set_of(parent[gi]) if parent.get(gi) is not None else per_gauge.get(default)
        if own is None:
            warn(f"Gauge {gauges[gi]['name']} has no {model} parameter set and there is no default")
            own = {}
        sets[gi] = own
        return own
    labels = basin.label[cells]
    # EF5 looks grids up by the cell's top-left corner (Grid::GetRefLoc)
    ref_lon = (cols * basin.dem.cell + float(basin.dem.left)).astype(np.float32)
    ref_lat = (float(basin.dem.top) - rows * basin.dem.cell).astype(np.float32)
    out = {}
    for name in names:
        per_label = np.full(len(gauges), np.nan, np.float32)
        for gi in np.unique(labels):
            per_label[gi] = set_of(int(gi)).get(name, np.nan)
        scalar = per_label[labels]
        if model == "crest" and name == "im" and name not in grids:
            scalar = scalar / np.float32(100.0)
        value = scalar.copy()
        gval = np.full(cells.size, np.nan, np.float32)
        if name in grids:
            path = os.path.join(run_dir, grids[name])
            g = Grid(path)
            if (g.rows, g.cols) == (basin.rows, basin.cols):
                if not np.allclose(g.gt, basin.dem.gt, rtol=0, atol=1e-9):
                    warn(f"{model} {name} grid has the DEM's size but a different extent "
                         f"({g.gt} vs {basin.dem.gt}); EF5 uses it cell by cell anyway")
                gy, gx, inside = rows, cols, np.ones(cells.size, bool)
            else:
                gy, gx, inside = g.loc(ref_lon, ref_lat)
                if (~inside).any():
                    record(problems, basin, cells[~inside], model, name, np.nan, value[~inside],
                           "outside grid", "cell is outside the grid; EF5 uses the scalar only")
            gval = g.data[gy, gx]
            is_nd = g.is_nodata(gval) & inside
            zero = (gval == 0) & inside
            used = np.where(zero, np.float32(0.01), gval)
            value = np.where(inside, scalar * used, scalar).astype(np.float32)
            if zero.any():
                record(problems, basin, cells[zero], model, name, gval[zero], value[zero],
                       "zero", "grid value 0; EF5 uses 0.01")
        else:
            is_nd = np.zeros(cells.size, bool)
        value = clamp(model, name, value, problems, basin, cells, gval, is_nd)
        if is_nd.any():
            record(problems, basin, cells[is_nd], model, name, gval[is_nd], value[is_nd],
                   "nodata", f"grid nodata ({g.nodata:g}) is used as a number; "
                   "ef5_value is what EF5 ends up with")
        nan = ~np.isfinite(value)
        if nan.any():
            record(problems, basin, cells[nan], model, name, gval[nan], value[nan],
                   "NaN", "parameter is NaN; EF5 will produce NaN here")
        out[name] = value
    return out


def clamp(model, name, value, problems, basin, cells, gval, is_nd):
    """The resets EF5 applies after reading (CRESTModel.cpp, KinematicRoute.cpp)."""
    neg, big = (lambda v: v < 0, "negative"), (lambda v: v > 1, "above 1")
    nan = (lambda v: np.isnan(v), "NaN")
    rules = {
        ("crest", "wm"): [(neg, 100.0)],
        ("crest", "im"): [(neg, 0.0), (big, 1.0)],
        ("crest", "b"): [(neg, 1.0), (nan, 0.0)],
        ("crest", "fc"): [(neg, 1.0)],
        ("kw", "leaki"): [(neg, 0.0), (big, 1.0)],
        ("kw", "alpha"): [(neg, 1.0)],
        ("kw", "alpha0"): [(neg, 1.0)],
        ("kw", "beta"): [(neg, 0.6)],
    }
    key = (model if model != "kw_parallel" else "kw", name)
    for (test, why), new in rules.get(key, []):
        hit = test(value)
        report = hit & ~is_nd                      # nodata cells get one row, as nodata
        if report.any():
            record(problems, basin, cells[report], model, name, gval[report],
                   np.full(report.sum(), new, np.float32), "reset",
                   f"value {why}; EF5 silently uses {new:g}")
        value = np.where(hit, np.float32(new), value)
    return value


def record(problems, basin, idx, model, name, grid_value, ef5_value, kind, note):
    problems.append({"cells": np.asarray(idx), "model": model, "param": name,
                     "grid": np.broadcast_to(np.asarray(grid_value, np.float32), np.shape(idx)),
                     "ef5": np.broadcast_to(np.asarray(ef5_value, np.float32), np.shape(idx)),
                     "kind": kind, "note": note})


# ------------------------------------------------------------------ checks

def check_fac(basin, gauges, warn):
    """FAC must not decrease downstream, and must count the upstream cells."""
    cells = np.nonzero(basin.model)[0]
    d = basin.down[cells]
    ok = d >= 0
    fac = basin.fam.data.ravel()
    dec = cells[ok][fac[d[ok]] < fac[cells[ok]]]
    if dec.size:
        warn(f"FAC decreases downstream at {dec.size} cells (FAC and flow directions "
             "disagree; channels can be cut where FAC drops below th)")
    # Count cells upstream of each gauge along the flow directions
    count = np.zeros(basin.rows * basin.cols, np.int64)
    inflows = np.zeros(basin.rows * basin.cols, np.int64)
    md = basin.down[cells]
    np.add.at(inflows, md[(md >= 0) & basin.model[np.maximum(md, 0)]], 1)
    queue = deque(cells[inflows[cells] == 0])     # headwater cells first
    while queue:
        idx = queue.popleft()
        dd = basin.down[idx]
        if dd >= 0 and basin.model[dd]:
            count[dd] += count[idx] + 1
            inflows[dd] -= 1
            if inflows[dd] == 0:
                queue.append(dd)
    for g in gauges:
        if g.get("dropped") or not basin.model[g["index"]]:
            continue
        expect = g["fac"] + (0 if basin.self_fam else 1)
        actual = count[g["index"]] + 1
        if actual > expect:
            warn(f"Gauge {g['name']}: flow directions give {actual} cells upstream but FAC "
                 f"allows {expect}; EF5 stops walking after {expect} and leaves the rest out")
    return dec


def nan_risk(basin, nan_cells, channel):
    """Follow each NaN cell downstream to the first channel cell it reaches."""
    entry = {}
    for idx in nan_cells:
        p = idx
        while p >= 0 and basin.model[p] and not channel[p]:
            p = basin.down[p]
        if p >= 0 and basin.model[p]:
            entry.setdefault(p, []).append(idx)
    return entry


def find_breaks(basin, maxq, channel, nan_mask, min_flow):
    """Channel cells with no flow in maxq that receive flow from upstream."""
    cells = np.nonzero(basin.model & (maxq >= min_flow))[0]
    d = basin.down[cells]
    ok = d >= 0
    d = d[ok]
    hit = basin.model[d] & channel[d] & ~(maxq[d] > 0)
    breaks = {}
    for b, u in zip(d[hit], cells[ok][hit]):
        breaks.setdefault(int(b), []).append(float(maxq[u]))
    out = []
    for b, ups in breaks.items():
        if nan_mask[b]:
            cause, src = "NaN parameter at this cell", [b]
        else:
            # Follow the dry cells upstream to the NaN cells feeding this one
            src, seen, queue = [], {b}, deque([b])
            while queue and len(seen) < 100000:
                x = queue.popleft()
                for u in basin.upstream(x):
                    if u in seen or not basin.model[u] or maxq[u] > 0:
                        continue
                    seen.add(u)
                    (src if nan_mask[u] else queue).append(u)
            cause = ("NaN from upstream (another branch joins here)" if src
                     else "no NaN parameter found upstream; check this cell")
        out.append((b, max(ups), cause, src))
    return sorted(out, key=lambda t: -t[1])


# ------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("control")
    ap.add_argument("--task", help="task to check (default: first task in [Execute])")
    ap.add_argument("--output", help="OUTPUT folder of a finished run, to find actual breaks")
    ap.add_argument("--maxq", help="maxq GeoTIFF to use (default: newest qpf_maxq/maxq in --output)")
    ap.add_argument("--run-dir", default=".", help="folder EF5 runs in (control-file paths are relative to it)")
    ap.add_argument("--prefix", default="ef5_check", help="prefix for the CSV files (default: ef5_check)")
    ap.add_argument("--all", action="store_true",
                    help="also write zero values, and channel-only parameters on hillslope cells, to the CSV")
    ap.add_argument("--min-flow", type=float, default=0.1,
                    help="upstream maxq (m3/s) that counts as flow for a break (default: 0.1)")
    args = ap.parse_args()

    def warn(msg):
        print(f"WARNING: {msg}")

    sections = parse_control(args.control)
    by = {}
    for typ, name, pairs in sections:
        by.setdefault(typ, {})[name] = pairs
    run = lambda p: os.path.join(args.run_dir, p)

    task_name = (args.task or first(by["execute"][""], "task")).lower()
    task = by["task"][task_name]
    model = first(task, "model").lower()
    routing = (first(task, "routing") or "").lower() or None
    basic = by["basic"][""]
    print(f"Task {task_name}: model {model}, routing {routing or 'none'}")

    dem, ddm, fam = Grid(run(first(basic, "dem"))), Grid(run(first(basic, "ddm"))), Grid(run(first(basic, "fam")))
    for g in (ddm, fam):
        if (g.rows, g.cols) != (dem.rows, dem.cols):
            sys.exit(f"{g.path} is not the size of the DEM")
    basin = Basin(dem, ddm, fam, is_true(first(basic, "esriddm")), is_true(first(basic, "selffam")))
    # LoadBasicGrids/FixFAM: a cell that is nodata in any basic grid is nodata in all
    fam.fixed = fam.data.copy()
    fam.fixed[~basin.valid] = np.float32(fam.nodata) if fam.nodata is not None else np.nan

    # Gauges of the task's basin
    gauges = []
    proj = (first(basic, "proj") or "geographic").lower()
    for gname in [v.lower() for k, v in by["basin"][first(task, "basin").lower()] if k == "gauge"]:
        gp = by["gauge"][gname]
        g = {"name": gname, "continue": not (first(gp, "continueupstream") or "true").lower() in ("false", "no")}
        if proj != "geographic" and (first(gp, "lat") is not None or first(gp, "basinarea") is not None):
            sys.exit(f"Gauge {gname}: placing gauges by lat/lon or basinarea is only supported "
                     "for PROJ=geographic (use cellx/celly)")
        if first(gp, "lat") is not None:
            lon, lat = float(first(gp, "lon")), float(first(gp, "lat"))
            y, x, inside = fam.loc(lon, lat)
            if not inside:
                warn(f"Gauge {gname} is outside the grid; EF5 drops it")
                continue
            y, x = int(y), int(x)
        else:
            x, y = int(first(gp, "cellx")), int(first(gp, "celly"))
            lat = float(fam.top) - y * fam.cell            # Grid::GetRefLoc
        g["given"] = (y, x)
        if first(gp, "basinarea") is not None:
            y, x = snap_gauge(fam, y, x, float(first(gp, "basinarea")), lat)
        if not basin.valid[y, x] or fam.data[y, x] < 0:
            warn(f"Gauge {gname} is in a nodata cell; EF5 stops with an error")
            continue
        g["index"], g["fac"] = y * dem.cols + x, float(fam.data[y, x])
        gauges.append(g)

    maxq = None
    if args.output or args.maxq:
        path = args.maxq or newest_maxq(args.output)
        mg = Grid(path)
        maxq = mg.data.ravel().astype(np.float64)
        print(f"Using {path}")

    parent = basin.carve(gauges, warn)
    if maxq is not None:
        run_cells = ~mg.is_nodata(mg.data).ravel()
        if (run_cells != basin.model).any():
            warn(f"Modelled cells differ from the run in {int((run_cells != basin.model).sum())} "
                 "cells; is this the output of this control file?")
    print(f"Modelled cells: {int(basin.model.sum())} (from {len(gauges)} gauges)")

    problems = []
    default = (first(task, "defaultparamsgauge") or "").lower()
    params = {}
    for kind, setkey, table in (("water balance", "param_set", MODEL_PARAMS),
                                ("routing", "routing_param_set", ROUTE_PARAMS)):
        mname = model if kind == "water balance" else routing
        if mname is None or first(task, setkey) is None:
            continue
        set_name = first(task, setkey).lower()
        set_type = "kwparamset" if mname == "kw_parallel" else f"{mname}paramset"
        if set_name not in by.get(set_type, {}):
            sys.exit(f"No [{set_type} {set_name}] section in the control file")
        pairs = by[set_type][set_name]
        per_gauge, grids = param_set_values(pairs, table[mname])
        params[mname] = cell_params(basin, gauges, parent, default, per_gauge, grids,
                                    table[mname], args.run_dir, mname, problems, warn)

    cells = np.nonzero(basin.model)[0]
    n = basin.rows * basin.cols
    channel = np.zeros(n, bool)
    if routing in ("kw", "kw_parallel", "lr"):
        channel[cells] = fam.data.ravel()[cells] > params[routing]["th"]
    nan_mask = np.zeros(n, bool)
    for p in problems:
        if p["kind"] == "NaN" and not (p["param"] in CHANNEL_ONLY and routing and p["model"] == routing):
            nan_mask[p["cells"]] = True
        elif p["kind"] == "NaN":
            nan_mask[p["cells"][channel[p["cells"]]]] = True

    severity = {"NaN": 0, "nodata": 1, "outside grid": 2, "reset": 3, "zero": 4}
    problems.sort(key=lambda p: (severity[p["kind"]], p["model"] != model))
    print("\n== Parameters (modelled cells only)")
    for p in problems:
        idx = p["cells"]
        where = ""
        if p["param"] in CHANNEL_ONLY and p["model"] == routing:
            where = f" ({int(channel[idx].sum())} on channel cells, where it is used)"
        print(f"  {p['model']:6s} {p['param']:7s} {p['kind']:13s} {idx.size:7d} cells{where}: {p['note']}")
    if not problems:
        print("  no problems found")

    check_fac(basin, gauges, warn)

    risk = nan_risk(basin, np.nonzero(nan_mask)[0], channel) if channel.any() else {}
    print(f"\n== Rivers at risk: NaN water reaches a channel at {len(risk)} cells "
          f"(from {int(nan_mask.sum())} cells with NaN parameters); a NaN only spreads when\n"
          "   the equation using it runs, so a wetter event can cut more of these rivers")

    breaks = None
    if maxq is not None:
        breaks = find_breaks(basin, maxq, channel, nan_mask, args.min_flow)
        print(f"\n== Breaks in this run: {len(breaks)} channel cells receive flow but never had any")
        for b, up, cause, src in breaks[:10]:
            lat, lon = cell_centre(dem, *divmod(b, dem.cols))
            print(f"  lat {lat:9.4f} lon {lon:10.4f}  FAC {fam.data.ravel()[b]:8.0f}  "
                  f"upstream maxq {up:9.1f}  {cause}")
        if len(breaks) > 10:
            print(f"  ... {len(breaks) - 10} more in {args.prefix}_breaks.csv")

    write_csvs(args.prefix, dem, fam, basin, gauges, channel, problems, risk, breaks, routing, args.all)
    print(f"\nWrote {args.prefix}_gauges.csv, {args.prefix}_params.csv, {args.prefix}_risk.csv"
          + (f", {args.prefix}_breaks.csv" if maxq is not None else ""))
    return 0


def snap_gauge(fam, y, x, area, lat):
    """BasicGrids.cpp CarveBasin: move the gauge to the cell within 20 km whose
    FAC best matches basinarea (geographic projection)."""
    lat = np.float32(lat)
    sn = np.float32(fam.cell * 110574)
    cell_area = np.float32(sn * np.float32(math.cos(math.radians(lat))) * sn / 1000000)
    target = np.float32(area) * np.float32(1.0 / cell_area)
    max_dist = max(int(20000.0 / sn), 2)
    d = fam.fixed
    nd = np.float32(fam.nodata) if fam.nodata is not None else None
    err = lambda yy, xx: np.float32(np.float32(d[yy, xx] - target) ** 2)
    best, by_, bx = err(y, x), y, x
    for dist in range(1, max_dist):
        for ty, tx, ok in ((y, x + dist, x + dist < fam.cols),
                           (y + dist, x + dist, x + dist < fam.cols and y + dist < fam.rows),
                           (y + dist, x, y + dist < fam.rows),
                           (y + dist, x - dist, x - dist > 0 and y + dist < fam.rows),
                           (y, x - dist, x - dist > 0),
                           (y - dist, x - dist, x - dist > 0 and y - dist > 0),
                           (y - dist, x, y - dist > 0),
                           (y - dist, x + dist, x + dist < fam.cols and y - dist > 0)):
            if ok and d[ty, tx] != nd:
                e = err(ty, tx)
                if best > e:
                    best, by_, bx = e, ty, tx
    return by_, bx


def newest_maxq(folder):
    for pattern in ("qpf_maxq.*.tif", "maxq.*.tif"):
        found = sorted(glob.glob(os.path.join(folder, pattern)))
        if found:
            return found[-1]
    sys.exit(f"No maxq GeoTIFF in {folder} (add MAXSTREAMFLOW to OUTPUT_GRIDS, or pass --maxq)")


def write_csvs(prefix, dem, fam, basin, gauges, channel, problems, risk, breaks, routing, everything):
    facv = fam.data.ravel()
    gname = lambda idx: gauges[basin.label[idx]]["name"] if basin.label[idx] >= 0 else ""

    def place(idx):
        r, c = divmod(int(idx), dem.cols)
        lat, lon = cell_centre(dem, r, c)
        return [f"{lat:.6f}", f"{lon:.6f}", r, c, f"{facv[idx]:g}", int(channel[idx]), gname(idx)]
    head = ["lat", "lon", "row", "col", "fac", "channel", "gauge"]

    with open(f"{prefix}_gauges.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["gauge", "lat", "lon", "row", "col", "fac", "moved_by_basinarea", "in_model"])
        for g in gauges:
            r, c = divmod(g["index"], dem.cols)
            lat, lon = cell_centre(dem, r, c)
            w.writerow([g["name"], f"{lat:.6f}", f"{lon:.6f}", r, c, f"{g['fac']:g}",
                        int((r, c) != g["given"]), int(not g.get("dropped") and basin.model[g["index"]])])

    with open(f"{prefix}_params.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(head + ["model", "param", "issue", "grid_value", "ef5_value", "note"])
        for p in problems:
            if p["kind"] == "zero" and not everything:
                continue
            unused = p["param"] in CHANNEL_ONLY and p["model"] == routing
            for idx, gv, ev in zip(p["cells"], p["grid"], p["ef5"]):
                if unused and not everything and not channel[idx]:
                    continue
                w.writerow(place(idx) + [p["model"], p["param"], p["kind"], f"{gv:g}", f"{ev:g}", p["note"]])

    with open(f"{prefix}_risk.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(head + ["nan_cells_upstream", "nearest_nan_lat", "nearest_nan_lon"])
        for idx, src in sorted(risk.items(), key=lambda kv: -facv[kv[0]]):
            near = place(src[0])
            w.writerow(place(idx) + [len(src), near[0], near[1]])

    if breaks is not None:
        with open(f"{prefix}_breaks.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(head + ["upstream_maxq", "cause", "nan_params_here", "source_lat", "source_lon", "nan_cells_found"])
            nan_here = {}
            for p in problems:
                if p["kind"] == "NaN":
                    for idx in p["cells"]:
                        nan_here.setdefault(int(idx), []).append(f"{p['model']}:{p['param']}")
            for b, up, cause, src in breaks:
                s = place(src[0]) if src else ["", ""]
                w.writerow(place(b) + [f"{up:.1f}", cause, " ".join(nan_here.get(b, [])),
                                       s[0], s[1], len(src)])


if __name__ == "__main__":
    sys.exit(main())
