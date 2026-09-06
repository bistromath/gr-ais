FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    castxml \
    cmake \
    doxygen \
    gdb \
    git \
    gnuradio \
    gnuradio-dev \
    graphviz \
    ninja-build \
    pkg-config \
    pybind11-dev \
    python3-dev \
    python3-numpy \
    python3-packaging \
    python3-pygccxml \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu reserves UID 1000; Dev Containers can adjust this user's UID on Linux.
RUN useradd --create-home --uid 1001 --shell /bin/bash vscode \
    && echo 'vscode ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/vscode \
    && chmod 0440 /etc/sudoers.d/vscode

USER vscode
