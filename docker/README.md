# Running EF5 in a container

The image is EF5 built on Ubuntu 26.04 with the system GDAL (3.12, so it has
Zarr output), libtiff and libgeotiff. You don't need compilers or libraries on
your own machine, and the same image gives identical results on every system.

There are three ways to get it:

1. **Pull the prebuilt image** that each release publishes:
   `ghcr.io/ahwalab/ef5:<version>` (for example `1.3.1`), or `latest` for the
   newest release.
2. **Build it yourself** from this repository with `docker/build.sh`.
3. **Use Apptainer** on an HPC cluster without Docker: pull the prebuilt image
   or build it from `ef5.def`.

| file | use |
|---|---|
| `Dockerfile` | Docker image |
| `ef5.def` | Apptainer (Singularity) image, for HPC clusters without Docker |
| `build.sh` | builds either image from this repository |
| `ef5-docker` | runs EF5 from the Docker image like a normal `ef5` binary |
| `compose.yaml` | Docker Compose service to copy into your own setup |

Only `configure.ac`, `Makefile.am` and `src/` go into the image.

In every case, run EF5 from the folder you normally run it in. Relative paths
in the control file are relative to that folder, which is mounted into the
container. Files outside it are not visible in the container unless you mount
them too.

## Docker

### Prebuilt image

```
docker pull ghcr.io/ahwalab/ef5:1.3.1
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" ghcr.io/ahwalab/ef5:1.3.1 control.txt
```

### Build the image yourself

Build it once (a few minutes), from anywhere in the repository:

```
docker/build.sh docker            # image "ef5"
```

or without cloning the repository:

```
docker build -t ef5 -f docker/Dockerfile https://github.com/AHWALab/EF5.git#v1.3.1
```

### Running

`ef5-docker` runs EF5 from the image like a normal `ef5` binary. It mounts the
current folder at the same path inside the container and runs EF5 as you, so
the outputs belong to you:

```
/path/to/EF5/docker/ef5-docker control.txt
/path/to/EF5/docker/ef5-docker --check control.txt
EF5_IMAGE=ghcr.io/ahwalab/ef5:1.3.1 /path/to/EF5/docker/ef5-docker control.txt
```

It uses the image `ef5` (built with `build.sh`) unless `EF5_IMAGE` names
another one. If the control file uses absolute paths outside the current
folder, add `-v` options for those folders to the `docker run` line in
`ef5-docker`. Without the wrapper, the same command is:

```
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" ef5 control.txt
```

### Docker Compose

Copy [`compose.yaml`](compose.yaml) into the folder with your control file, or
add its `ef5` service to your own compose file, then:

```
EF5_USER="$(id -u):$(id -g)" docker compose run --rm ef5 control.txt
```

The service mounts the folder at `/data`. It pulls the prebuilt image; to build
from source instead, uncomment its `build:` lines. For a fixed setup, put the
control file in the command:

```yaml
services:
  ef5:
    image: ghcr.io/ahwalab/ef5:1.3.1
    user: "1000:1000"
    working_dir: /data
    volumes:
      - /path/to/run:/data
      - /path/to/forcings:/path/to/forcings:ro   # data the control file names
    environment:
      OMP_NUM_THREADS: "8"
    command: ["control.txt"]
```

## Apptainer (HPC)

Pull the prebuilt image into a single file you can copy anywhere (no root
needed):

```
apptainer pull ef5.sif docker://ghcr.io/ahwalab/ef5:1.3.1
```

or build it from this repository (needs root or `--fakeroot`, depending on how
Apptainer is set up; if the cluster does not allow building, build on another
machine and copy `ef5.sif` over):

```
docker/build.sh apptainer          # writes ef5.sif
```

On a machine with Docker you can also convert a local image:
`apptainer build ef5.sif docker-daemon://ef5:latest`.

Run it (Apptainer mounts your home and the current folder automatically):

```
apptainer run ef5.sif control.txt
apptainer run ef5.sif --check control.txt
```

On data outside your home, add `--bind /path/to/data`. In a batch job, run
the same command from the job script, with `OMP_NUM_THREADS` set to the cores
the job asked for.

## Threads

EF5 uses OpenMP. In the containers it uses **4 threads** by default. Set
`OMP_NUM_THREADS` as usual for more or fewer:

```
OMP_NUM_THREADS=8 docker/ef5-docker control.txt        # ef5-docker passes it on
docker run -e OMP_NUM_THREADS=8 ...                     # plain docker run
OMP_NUM_THREADS=8 docker compose run --rm ef5 ...       # compose.yaml passes it on
OMP_NUM_THREADS=8 apptainer run ef5.sif control.txt     # Apptainer
```

Small domains are often faster with a few threads than with all cores: a 1 km
national domain took 9.2 s with 4 threads and 11.8 s with 16.

Docker's `--cpus` option does not reduce the number of threads. EF5 still
starts one thread per CPU it can see (or `OMP_NUM_THREADS`), and they then
share the CPU time `--cpus` allows, which is slow. To limit EF5, set
`OMP_NUM_THREADS`, or use `--cpuset-cpus`.
