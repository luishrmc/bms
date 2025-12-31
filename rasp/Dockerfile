# syntax=docker/dockerfile:1
#
# Raspberry Pi 32-bit (arm/v7) runtime image for pre-compiled bin/bms

FROM --platform=linux/arm/v7 arm32v7/debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Sao_Paulo

WORKDIR /opt/bms

RUN apt-get update && apt-get install -y --no-install-recommends \
    # Shell + basic runtime tools used by entrypoint/scripts
    bash ca-certificates tzdata curl jq pkg-config wget \
    # C/C++ runtime
    libc6 libstdc++6 libgcc-s1 \
    # TLS + HTTP
    libssl3 libcurl4 \
    # Modbus library
    libmodbus-dev \
    # Boost (broad, safe option; you can later tighten using ldd)
    libboost-all-dev \
    # Json library
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# Non-root runtime user
RUN useradd -m -u 1000 -s /bin/bash bms

# If you bake the whole folder into the image, keep this.
# If you mount everything via volumes, you can omit COPY . .
COPY . /opt/bms

# Ensure executables have permissions (covers scp/rsync edge cases)
RUN chmod +x /opt/bms/bin/bms \
    && chmod +x /opt/bms/entrypoint.sh \
    && chmod +x /opt/bms/scripts/get_token.sh \
    && chmod +x /opt/bms/scripts/setup_schema.sh

USER bms
ENTRYPOINT ["/opt/bms/entrypoint.sh"]
