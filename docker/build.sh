#!/usr/bin/env bash
# Build the EF5 container image (run from anywhere in the repository).
#
#   docker/build.sh docker [tag]        -> Docker image (default tag: ef5)
#   docker/build.sh apptainer [file]    -> Apptainer image (default: ef5.sif)
set -euo pipefail
cd "$(dirname "$0")/.."
case "${1:-}" in
  docker)    docker build -f docker/Dockerfile -t "${2:-ef5}" . ;;
  apptainer) apptainer build "${2:-ef5.sif}" docker/ef5.def ;;
  *)         sed -n '2,6p' "$0"; exit 2 ;;
esac
