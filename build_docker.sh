#!/bin/bash
set -e

# Build the docker image (tags it as solarsweeper-build-env)
docker build -t solarsweeper-build-env -f Dockerfile.build .

# Run the build inside the container, mounting the current directory
echo "Building project inside Docker container..."
docker run --rm -v "$(pwd):/app" solarsweeper-build-env bash -c "cmake -B build_docker && cmake --build build_docker"
echo "Build complete! Binary is in build_docker/"
