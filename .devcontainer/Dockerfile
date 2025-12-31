# syntax=docker/dockerfile:1

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV PIP_DISABLE_PIP_VERSION_CHECK=1
ENV PIP_NO_CACHE_DIR=1
# Ensure the path for local pip packages is included
ENV PATH="/home/bms/.local/bin:${PATH}" 
ENV TZ=America/Sao_Paulo

# --- Stage 1: Install Core Utilities and Bash ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    sudo ca-certificates jq tzdata curl lsb-release wget git openssh-client pkg-config unzip zip locales

# --- Stage 2: Create the 'bms' user ---
ARG USERNAME=bms
ARG USER_UID=1000
ARG USER_GID=$USER_UID
RUN groupadd --gid $USER_GID $USERNAME \
    && useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo $USERNAME ALL=\(ALL\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

# --- Stage 3: Install Remaining Dependencies ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Python 3.12 + pip/venv
    python3 python3-pip python3-venv python3-dev \
    # General graphics/font libs
    libgl1 libfontconfig1 libfreetype6 libglib2.0-0 fonts-dejavu-core \
    libssl-dev liblz4-dev libre2-dev zlib1g-dev libcurl4-openssl-dev \
    # C/C++ toolchain + build tools
    build-essential cmake ninja-build gdb lldb cppcheck ccache \
    # Clean up apt lists to keep the image small
    # C++ Libraries
    libboost-all-dev \
    nlohmann-json3-dev \
    libmodbus-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Stage 4: Set Timezone ---
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# --- Stage 5: Cross-compile tools ---
RUN dpkg --add-architecture armhf \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
        # Target (armhf) libraries needed at link time
        libboost-system-dev:armhf \
        libboost-thread-dev:armhf \
        libboost-chrono-dev:armhf \
        libmodbus-dev:armhf \
        libcurl4-openssl-dev:armhf \
        libssl-dev:armhf \
        zlib1g-dev:armhf \
    && rm -rf /var/lib/apt/lists/*
