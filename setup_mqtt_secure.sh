#!/bin/bash

USER="lumac"
PASSWORD="128Parsecs!"

set -e

# 1. Create directory structure
echo "📁 Creating Mosquitto configuration directories..."
mkdir -p .devcontainer/mosquitto/{config,data,log,certs}

# 2. Generate self-signed TLS certificate
echo "🔐 Generating TLS certificate and key..."
openssl req -x509 -newkey rsa:2048 \
  -keyout .devcontainer/mosquitto/certs/bms_broker.key \
  -out .devcontainer/mosquitto/certs/bms_broker.crt \
  -days 365 -nodes \
  -subj "/CN=localhost"

# 3. Create Mosquitto password file
echo "🔒 Creating Mosquitto password file..."
touch .devcontainer/mosquitto/config/passwordfile
docker run --rm -v "$(pwd)/.devcontainer/mosquitto/config:/mosquitto" eclipse-mosquitto \
  mosquitto_passwd -b /mosquitto/passwordfile $USER $PASSWORD

# Restrict permissions
chmod 700 .devcontainer/mosquitto/config/passwordfile
chown root:root .devcontainer/mosquitto/config/passwordfile


# 4. Generate mosquitto.conf
echo "⚙️ Writing mosquitto.conf..."
cat > .devcontainer/mosquitto/config/mosquitto.conf <<EOF
persistence true
persistence_location /mosquitto/data/

log_dest file /mosquitto/log/mosquitto.log

# TLS Listener
listener 8883
cafile /mosquitto/config/certs/bms_broker.crt
certfile /mosquitto/config/certs/bms_broker.crt
keyfile /mosquitto/config/certs/bms_broker.key

# Authentication
allow_anonymous false
password_file /mosquitto/config/passwordfile
EOF

chmod -R a+rw .devcontainer/mosquitto/log
chmod -R a+rw .devcontainer/mosquitto/data
chmod -R 755 .devcontainer/mosquitto
chmod 644 .devcontainer/mosquitto/certs/bms_broker.key
chmod 644 .devcontainer/mosquitto/certs/bms_broker.crt

cp .devcontainer/mosquitto/certs/bms_broker.crt ./certs/bms_broker.crt

echo "✅ Mosquitto secure setup complete!"
