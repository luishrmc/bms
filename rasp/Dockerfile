# syntax=docker/dockerfile:1
FROM --platform=linux/arm/v7 debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Sao_Paulo

# Runtime dependencies (no build tools)
# Note: Boost runtime package names can vary by Debian release; if apt complains,
# replace these with the appropriate libboost-*-* packages for your base image.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates tzdata \
    libstdc++6 libgcc-s1 \
    libssl3 libcurl4 \
    libboost-all-dev \
    nlohmann-json3-dev \
    libmodbus-dev \
    && rm -rf /var/lib/apt/lists/*

# Non-root runtime user
RUN useradd -m -u 1000 -s /bin/bash bms

WORKDIR /opt/bms
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER bms
ENTRYPOINT ["/entrypoint.sh"]
