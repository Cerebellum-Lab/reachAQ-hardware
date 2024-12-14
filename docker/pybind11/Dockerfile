#
# Build a Docker image with tools to build a pybind11 package
# Update the container image by running:
# $ docker buildx build --platform linux/arm64,linux/amd64 -t registry.gitlab.leaflabs.com/cuanschutz/autotrainer/pybind11 --push .
#

FROM ubuntu AS build

RUN apt update && \
    DEBIAN_FRONTEND=noninteractive apt install -y --no-install-recommends \
    build-essential \
    git \
    ninja-build \
    wget \
    python3 \
    python3-dev \
    python3-pip

# Install a recent version of cmake
RUN cd /usr/local && \
    wget https://github.com/Kitware/CMake/releases/download/v3.31.2/cmake-3.31.2-linux-$(uname -p).sh && \
    /bin/sh cmake-*.sh --skip-license && \
    rm cmake-*.sh

RUN pip3 install --break-system-packages pybind11 mypy pytest
