# Build EF5 in a container, so it compiles the same way on any system.
#
#   docker build -f docker/Dockerfile -t ef5 .
#   docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/data" ef5 control.txt
#
# Run from the top of the EF5 repository. The control file and every path in
# it are relative to /data (the folder mounted with -v). See docker/README.md.

# ---- Build stage: compilers and development files ----
FROM ubuntu:24.04 AS build

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential autoconf automake libtool pkg-config \
      libgdal-dev libgeotiff-dev libtiff-dev zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY configure.ac Makefile.am ./
COPY src ./src
RUN autoreconf --force --install \
 && ./configure \
 && make -j"$(nproc)"

# ---- Runtime stage: only ef5 and the libraries it needs ----
FROM ubuntu:24.04

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libgdal34t64 libgeotiff5 libtiff6 zlib1g libgomp1 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/bin/ef5 /usr/local/bin/ef5

# 4 OpenMP threads unless OMP_NUM_THREADS is given (docker run -e ...)
ENV OMP_NUM_THREADS=4

WORKDIR /data
ENTRYPOINT ["ef5"]
