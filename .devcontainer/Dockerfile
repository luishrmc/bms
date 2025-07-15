# syntax=docker/dockerfile:1

FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

ARG USERNAME=lumac

# Create the user first (needed before switching user)
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    sudo \
    ca-certificates \
    tzdata && \
    groupadd --gid 1000 $USERNAME && \
    useradd --uid 1000 --gid 1000 -m $USERNAME && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME

# Install development tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gdb \ 
    # lldb \
    clang \
    clang-format \
    clang-tidy \
    cppcheck \
    ccache \
    git \
    pkg-config \
    libssl-dev \
    curl unzip zip && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Switch to user and working directory
USER $USERNAME
WORKDIR /home/$USERNAME/workspace

# Default build command when container starts
CMD ["/bin/bash", "-c", "cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build"]
