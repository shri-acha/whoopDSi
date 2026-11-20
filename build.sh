#!/usr/bin/env sh
# Build whoopDSi.nds with the official devkitPro Docker image (no local toolchain needed).
#   ./build.sh         -> build whoopDSi.nds
#   ./build.sh clean   -> remove build outputs
set -e
cd "$(dirname "$0")"
exec docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src devkitpro/devkitarm make "$@"
