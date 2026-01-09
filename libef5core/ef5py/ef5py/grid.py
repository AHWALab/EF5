"""
Grid class for ef5py.

Handles loading DEM, flow direction, and flow accumulation rasters,
and converting them to the internal GridCell structure.
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple, Union
import numpy as np

try:
    import rasterio
    HAS_RASTERIO = True
except ImportError:
    HAS_RASTERIO = False

try:
    from .. import core
    HAS_CORE = True
except ImportError:
    HAS_CORE = False


@dataclass
class GridInfo:
    """Grid metadata from raster files."""
    rows: int
    cols: int
    cell_size: float  # degrees or meters
    x_origin: float
    y_origin: float
    crs: str
    nodata: float


class Grid:
    """
    Hydrological grid with topology.
    
    The Grid represents the spatial domain for EF5 simulations,
    including elevation, flow direction, and connectivity.
    
    Parameters
    ----------
    dem : ndarray
        Digital elevation model (2D array)
    fdir : ndarray
        Flow direction (2D array, ESRI convention)
    fac : ndarray, optional
        Flow accumulation (2D array)
    info : GridInfo
        Grid metadata (resolution, origin, CRS)
        
    Examples
    --------
    >>> grid = Grid.from_tifs("dem.tif", "fdir.tif", "fac.tif")
    >>> print(f"Grid: {grid.rows} x {grid.cols}, {grid.n_cells} valid cells")
    """
    
    def __init__(
        self,
        dem: np.ndarray,
        fdir: np.ndarray,
        fac: Optional[np.ndarray] = None,
        info: Optional[GridInfo] = None,
        mask: Optional[np.ndarray] = None
    ):
        self.dem = dem.astype(np.float32)
        self.fdir = fdir.astype(np.int32)
        self.fac = fac.astype(np.float32) if fac is not None else None
        self.info = info
        
        self.rows, self.cols = dem.shape
        
        # Create mask (valid cells)
        if mask is not None:
            self.mask = mask.astype(bool)
        else:
            self.mask = ~np.isnan(self.dem) & (self.dem > -9999)
        
        self.n_cells = int(self.mask.sum())
        
        # Build cell list and topology (lazy)
        self._cells = None
        self._topology = None
        self._cell_indices = None
    
    @classmethod
    def from_tifs(
        cls,
        dem: str,
        fdir: str,
        fac: Optional[str] = None,
        esri_fdir: bool = True
    ) -> "Grid":
        """
        Create Grid from GeoTIFF files.
        
        Parameters
        ----------
        dem : str
            Path to DEM raster
        fdir : str
            Path to flow direction raster
        fac : str, optional
            Path to flow accumulation raster
        esri_fdir : bool
            If True, flow direction uses ESRI convention (powers of 2)
            
        Returns
        -------
        Grid
            Initialized grid with topology
        """
        if not HAS_RASTERIO:
            raise ImportError("rasterio required to load TIF files. Install with: pip install rasterio")
        
        # Load DEM
        with rasterio.open(dem) as src:
            dem_data = src.read(1)
            nodata = src.nodata
            transform = src.transform
            crs = str(src.crs) if src.crs else "unknown"
            
            info = GridInfo(
                rows=src.height,
                cols=src.width,
                cell_size=abs(transform.a),
                x_origin=transform.c,
                y_origin=transform.f,
                crs=crs,
                nodata=nodata if nodata else -9999
            )
        
        # Mask nodata
        if nodata is not None:
            dem_data = np.where(dem_data == nodata, np.nan, dem_data)
        
        # Load flow direction
        with rasterio.open(fdir) as src:
            fdir_data = src.read(1)
        
        # Load flow accumulation
        fac_data = None
        if fac:
            with rasterio.open(fac) as src:
                fac_data = src.read(1)
        
        return cls(dem_data, fdir_data, fac_data, info)
    
    @classmethod
    def from_arrays(
        cls,
        dem: np.ndarray,
        fdir: np.ndarray,
        fac: Optional[np.ndarray] = None,
        cell_size: float = 0.01,
        x_origin: float = 0.0,
        y_origin: float = 0.0
    ) -> "Grid":
        """Create Grid from numpy arrays."""
        info = GridInfo(
            rows=dem.shape[0],
            cols=dem.shape[1],
            cell_size=cell_size,
            x_origin=x_origin,
            y_origin=y_origin,
            crs="unknown",
            nodata=-9999
        )
        return cls(dem, fdir, fac, info)
    
    def get_cell_indices(self) -> np.ndarray:
        """Get (row, col) indices of valid cells."""
        if self._cell_indices is None:
            rows, cols = np.where(self.mask)
            self._cell_indices = np.column_stack([rows, cols])
        return self._cell_indices
    
    def build_cells(self):
        """Build GridCell array for C++ core."""
        if not HAS_CORE:
            raise ImportError("ef5py core not available")
        
        if self._cells is not None:
            return self._cells
        
        self._cells = core.make_grid_cells(self.n_cells)
        indices = self.get_cell_indices()
        
        for i, (row, col) in enumerate(indices):
            cell = self._cells[i]
            cell.x = int(col)
            cell.y = int(row)
            cell.slope = self._compute_slope(row, col)
            cell.area = self._compute_area(row, col)
            cell.hor_len = self.info.cell_size * 111000 if self.info else 1000
            
            if self.fac is not None:
                cell.fac = float(self.fac[row, col])
                cell.contrib_area = cell.fac * cell.area
            
            # Determine downstream
            downstream = self._get_downstream(row, col)
            if downstream is not None:
                # Find index in cell list
                try:
                    ds_idx = np.where((indices[:, 0] == downstream[0]) & 
                                     (indices[:, 1] == downstream[1]))[0]
                    if len(ds_idx) > 0:
                        cell.downstream_index = int(ds_idx[0])
                except:
                    cell.downstream_index = -1
            else:
                cell.downstream_index = -1
        
        return self._cells
    
    def _compute_slope(self, row: int, col: int) -> float:
        """Compute slope at cell using 8-neighbor gradient."""
        if row <= 0 or row >= self.rows - 1 or col <= 0 or col >= self.cols - 1:
            return 0.01
        
        dz_dx = (self.dem[row, col+1] - self.dem[row, col-1]) / (2 * self.info.cell_size)
        dz_dy = (self.dem[row+1, col] - self.dem[row-1, col]) / (2 * self.info.cell_size)
        
        slope = np.sqrt(dz_dx**2 + dz_dy**2)
        return max(float(slope), 0.001)
    
    def _compute_area(self, row: int, col: int) -> float:
        """Compute cell area in km²."""
        if self.info is None:
            return 1.0
        
        if 'geographic' in self.info.crs.lower() or self.info.cell_size < 1:
            # Geographic coords - approximate
            lat = self.info.y_origin - row * self.info.cell_size
            lat_rad = np.radians(lat)
            km_per_deg_lat = 111.0
            km_per_deg_lon = 111.0 * np.cos(lat_rad)
            return self.info.cell_size * km_per_deg_lat * self.info.cell_size * km_per_deg_lon
        else:
            # Projected coords (assume meters)
            return (self.info.cell_size / 1000) ** 2
    
    def _get_downstream(self, row: int, col: int) -> Optional[Tuple[int, int]]:
        """Get downstream cell based on flow direction (ESRI convention)."""
        fd = self.fdir[row, col]
        
        # ESRI flow direction: 1=E, 2=SE, 4=S, 8=SW, 16=W, 32=NW, 64=N, 128=NE
        directions = {
            1: (0, 1),    # E
            2: (1, 1),    # SE
            4: (1, 0),    # S
            8: (1, -1),   # SW
            16: (0, -1),  # W
            32: (-1, -1), # NW
            64: (-1, 0),  # N
            128: (-1, 1)  # NE
        }
        
        if fd not in directions:
            return None
        
        dr, dc = directions[fd]
        new_row, new_col = row + dr, col + dc
        
        if 0 <= new_row < self.rows and 0 <= new_col < self.cols:
            if self.mask[new_row, new_col]:
                return (new_row, new_col)
        
        return None
    
    def get_cell_at(self, lat: float, lon: float) -> Optional[int]:
        """
        Get cell index at lat/lon coordinates.
        
        Returns None if outside grid or on nodata.
        """
        if self.info is None:
            return None
        
        col = int((lon - self.info.x_origin) / self.info.cell_size)
        row = int((self.info.y_origin - lat) / self.info.cell_size)
        
        if 0 <= row < self.rows and 0 <= col < self.cols:
            if self.mask[row, col]:
                # Find in cell list
                indices = self.get_cell_indices()
                idx = np.where((indices[:, 0] == row) & (indices[:, 1] == col))[0]
                if len(idx) > 0:
                    return int(idx[0])
        return None
    
    def __repr__(self):
        return f"Grid({self.rows}x{self.cols}, {self.n_cells:,} cells)"
