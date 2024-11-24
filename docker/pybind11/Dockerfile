#
# Build a Docker image with tools to build a pybind11 package
# Update the container image by running:
# $ docker buildx build -t registry.gitlab.leaflabs.com/cuanschutz/autotrainer/pybind11 .
#
# Update the container registry by running:
# $ docker push registry.gitlab.leaflabs.com/cuanschutz/autotrainer/pybind11
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
    wget https://github.com/Kitware/CMake/releases/download/v3.31.0/cmake-3.31.0-linux-x86_64.sh && \
    /bin/sh cmake-3.31.0-linux-x86_64.sh --skip-license && \
    rm cmake-3.31.0-linux-x86_64.sh

RUN pip3 install pybind11 mypy pytest
