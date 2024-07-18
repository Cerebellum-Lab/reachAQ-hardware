#
# Build a Docker image with tools to build a zephyr firmware image
# Update the container image by running:
# $ docker buildx build -t registry.gitlab.leaflabs.com/cuanschutz/autotrainer/zephyr-sdk .
#
# Update the container registry by running:
# $ docker push registry.gitlab.leaflabs.com/cuanschutz/autotrainer/zephyr-sdk
#

FROM ubuntu as build

RUN apt update && \
    DEBIAN_FRONTEND=noninteractive apt install -y --no-install-recommends \
    git \
    cmake \
    ninja-build \
    gperf \
    ccache \
    dfu-util \
    device-tree-compiler \
    wget \
    python3-dev \
    python3-pip \
    python3-setuptools \
    python3-tk \
    python3-wheel \
    xz-utils \
    file \
    libpython3-dev \
    make \
    gcc \
    gcc-multilib \
    g++-multilib \
    libsdl2-dev \
    libmagic1 \
    protobuf-compiler \
    build-essential \
    python3-pyelftools

RUN python3 -m pip install \
    west \
    protobuf \
    pyelftools \
    click \
    cryptography \
    cbor \
    intelhex

RUN cd /opt && \
    wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz && \
    tar xf zephyr-sdk-*.tar.xz && \
    rm -f zephyr-sdk-*.tar.xz && \
    cd zephyr-sdk-* && \
    ./setup.sh -t arm-zephyr-eabi -c -h


