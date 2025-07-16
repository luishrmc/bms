#!/bin/bash

USER="lumac"
PASSWORD="128Parsecs!"

set -e

# 1. Create directory structure
echo "📁 Creating Mosquitto configuration directories..."
mkdir -p config/mosquitto/{config,data,log,certs}

# 2. Generate self-signed TLS certificate
echo "🔐 Generating TLS certificate and key..."
openssl req -x509 -nodes -days 365 \
  -newkey rsa:2048 \
  -keyout config/mosquitto/certs/bms_broker.key \
  -out config/mosquitto/certs/bms_broker.crt \
  -config config/openssl.cnf \
  -extensions v3_req

# 3. Create Mosquitto password file
echo "🔒 Creating Mosquitto password file..."
touch config/mosquitto/config/passwordfile
docker run --rm -v "$(pwd)/config/mosquitto/config:/mosquitto" eclipse-mosquitto \
  mosquitto_passwd -b /mosquitto/passwordfile $USER $PASSWORD

# Restrict permissions
chmod 700 config/mosquitto/config/passwordfile
chown root:root config/mosquitto/config/passwordfile


# 4. Generate mosquitto.conf
echo "⚙️ Writing mosquitto.conf..."
cat > config/mosquitto/config/mosquitto.conf <<EOF
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

chmod -R a+rw config/mosquitto/log
chmod -R a+rw config/mosquitto/data
chmod -R 755 config/mosquitto
chmod 644 config/mosquitto/certs/bms_broker.key
chmod 644 config/mosquitto/certs/bms_broker.crt

echo "✅ Mosquitto secure setup complete!"
