AC_INIT([Ensemble Framework For Flash Flood Forecasting], [0.1], [zac.flamig@noaa.gov],
             [ef5], [http://hydro.ou.edu/])
AC_PREREQ([2.62])
AC_PROG_CXX
AC_OPENMP
AC_SUBST(OPENMP_CFLAGS)
PKG_PROG_PKG_CONFIG
# Zarr output (OUTPUT_TYPE=ZARR) is written with GDAL, which must be 3.11 or
# newer: older versions write a draft of Zarr v3 that zarr-python and xarray
# cannot read. Without it EF5 is built with GeoTIFF output only.
AC_ARG_WITH([zarr],
  [AS_HELP_STRING([--without-zarr],
    [build without Zarr output (no GDAL needed); default: build it if GDAL >= 3.11 is found])],
  [], [with_zarr=check])
ZARR_OUTPUT=no
if test "x$with_zarr" != xno; then
  PKG_CHECK_MODULES([GDAL], [gdal >= 3.11.0],
    [ZARR_OUTPUT=yes
     GDAL_CFLAGS="$GDAL_CFLAGS -DEF5_ZARR"],
    [if test "x$with_zarr" = xyes; then
       AC_MSG_ERROR([Zarr output needs GDAL 3.11 or newer: $GDAL_PKG_ERRORS])
     fi
     GDAL_CFLAGS=""
     GDAL_LIBS=""
     AC_MSG_WARN([GDAL 3.11 or newer not found: building EF5 without Zarr output (OUTPUT_TYPE=ZARR will stop with an error)])])
fi
AC_SUBST(GDAL_CFLAGS)
AC_SUBST(GDAL_LIBS)

AC_CANONICAL_HOST
case $host_os in
    *mingw*)
        CFLAGS="-static -static-libgcc -I/usr/x86_64-w64-mingw32/sys-root/mingw/include $CFLAGS"
        CXXFLAGS="-static -static-libgcc -I/usr/x86_64-w64-mingw32/sys-root/mingw/include $CXXFLAGS"
        CPPFLAGS="-static -static-libgcc -I/usr/x86_64-w64-mingw32/sys-root/mingw/include $CPPFLAGS"
        LDFLAGS="-static -static-libgcc -L/usr/x86_64-w64-mingw32/sys-root/mingw/lib $LDFLAGS"
	WINDOWS=yes
	AC_CHECK_TOOL(WINDRES, windres, no)	
        ;;
     *)
	;;
esac
AM_CONDITIONAL(WINDOWS, test x$WINDOWS = xyes)
#AC_CHECK_LIB([z], [gzread], [],[
#         echo "z library is required for this EF5"
#         exit -1])
#AC_CHECK_LIB([tiff], [TIFFReadScanline], [],[
#         echo "Tiff library is required for this EF5"
#         exit -1])
#AC_CHECK_LIB([geotiff], [GTIFNew], [],[
#         echo "GeoTiff library is required for this EF5"
#         exit -1])
#if test x$WINDOWS != xyes; then
#AC_CHECK_LIB([gomp], [omp_get_wtime], [],[
#         echo "gomp library is required for this EF5"
#         exit -1])
#fi;
LIBS="$LIBS -lz -ltiff -lgeotiff"
AC_CHECK_HEADERS(xtiffio.h)
AC_CHECK_HEADERS(geotiff/xtiffio.h)
AC_CHECK_HEADERS(libgeotiff/xtiffio.h)
if test x"$ac_cv_header_geotiff_xtiffio_h" = x"yes"; then
        CPPFLAGS="-I/usr/include/geotiff $CPPFLAGS";
else
        if test x"$ac_cv_header_libgeotiff_xtiffio_h" = x"yes"; then
                CPPFLAGS="-I/usr/include/libgeotiff $CPPFLAGS";
        else
                if test x"$ac_cv_header_xtiffio_h" != x"yes"; then
                        AC_MSG_ERROR([xtiffio.h header not found]);
                fi;
        fi;
fi;
AM_INIT_AUTOMAKE([1.9.6 -Wall -Werror no-define foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
AC_MSG_NOTICE([Zarr output: $ZARR_OUTPUT])
