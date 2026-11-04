# Building and running EF5 in a container

The container builds EF5 on Ubuntu 24.04 with the system GDAL, libtiff and
libgeotiff. You don't need compilers or libraries on your own machine. The
same build gives identical results on every system.

| file | use |
|---|---|
| `Dockerfile` | Docker image |
| `ef5.def` | Apptainer (Singularity) image, for HPC clusters without Docker |
| `build.sh` | builds either image |
| `ef5-docker` | runs EF5 from the Docker image like a normal `ef5` binary |

Only `configure.ac`, `Makefile.am` and `src/` go into the image.

## Docker

Build the image once (a few minutes), from anywhere in the repository:

```
docker/build.sh docker
```

Run EF5 from the folder you normally run it in:

```
/path/to/EF5/docker/ef5-docker control.txt
/path/to/EF5/docker/ef5-docker --check control.txt
```

`ef5-docker` mounts the current folder at the same path inside the container
and runs EF5 as you. Relative paths in the control file then work as usual,
and the outputs belong to you. Files outside the current folder are not
visible in the container. If the control file uses absolute paths elsewhere,
add `-v` options for those folders to the `docker run` line in `ef5-docker`.

Without the wrapper, the same command is:

```
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" ef5 control.txt
```

## Apptainer

Build the image (`ef5.sif`, a single file you can copy to any cluster). Run
this from the top of the repository:

```
docker/build.sh apptainer
```

Building needs root or `--fakeroot`, depending on how Apptainer is set up. If
the cluster does not allow building, build on another machine and copy
`ef5.sif` over. On a machine with Docker you can also convert the Docker
image: `apptainer build ef5.sif docker-daemon://ef5:latest`.

Run it (Apptainer mounts your home and the current folder automatically):

```
apptainer run ef5.sif control.txt
apptainer run ef5.sif --check control.txt
```

On data outside your home, add `--bind /path/to/data`.

## Threads

EF5 uses OpenMP. In the containers it uses **4 threads** by default. Set
`OMP_NUM_THREADS` as usual for more or fewer:

```
OMP_NUM_THREADS=8 docker/ef5-docker control.txt        # ef5-docker passes it on
docker run -e OMP_NUM_THREADS=8 ...                     # plain docker run
OMP_NUM_THREADS=8 apptainer run ef5.sif control.txt     # Apptainer
```

Small domains are often faster with a few threads than with all cores: the
Cuba 1 km run took 9.2 s with 4 threads and 11.8 s with 16.

Docker's `--cpus` option does not reduce the number of threads. EF5 still
starts one thread per CPU it can see (or `OMP_NUM_THREADS`), and they then
share the CPU time `--cpus` allows, which is slow. To limit EF5, set
`OMP_NUM_THREADS`, or use `--cpuset-cpus`.
