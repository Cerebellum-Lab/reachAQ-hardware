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
    cmake \
    ninja-build \
    wget \
    python3 \
    python3-dev \
    python3-pip

RUN pip3 install pybind11 mypy pytest
