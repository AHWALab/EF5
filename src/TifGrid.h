#ifndef TIF_GRID_H
#define TIF_GRID_H

#include "Grid.h"

FloatGrid *ReadFloatTifGrid(const char *file);
FloatGrid *ReadFloatTifGrid(const char *file, FloatGrid *incGrid);
void WriteFloatTifGrid(const char *file, FloatGrid *grid,
                       const char *artist = NULL, const char *datetime = NULL,
                       const char *copyright = NULL);
LongGrid *ReadLongTifGrid(const char *file);
// True if file is a TIFF stored as separate image planes
// (PlanarConfiguration=2). False if it is one image plane or cannot be opened.
bool IsTifSeparatePlanes(const char *file);

#endif
