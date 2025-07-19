#!/bin/bash

USER="lumac"
PASSWORD="128Parsecs!"
OPENSSL_CONFIG="config/openssl.cnf"

set -e

# 1. Create directory structure
echo "📁 Creating Mosquitto configuration directories..."
mkdir -p config/mosquitto/{config,data,log,certs}
mkdir -p config/mosquitto/certs/clients

# 2. Create CA key and certificate
echo "🏛️ Creating CA certificate..."

# Generate private key for the CA
openssl genrsa -out config/mosquitto/certs/ca.key 2048

# Create a self-signed root certificate using the CA key
openssl req -x509 -new -nodes -key config/mosquitto/certs/ca.key \
  -sha256 -days 365 \
  -out config/mosquitto/certs/ca.crt \
  -config "$OPENSSL_CONFIG" \
  -extensions v3_ca \
  -subj "/C=BR/ST=Minas Gerais/L=Belo Horizonte/O=UFMG/OU=DELT/CN=BMS_CA"

# 3. Create broker key and certificate signed by CA
echo "🔐 Creating broker certificate signed by CA..."

# Generate private key for the broker
openssl genrsa -out config/mosquitto/certs/broker.key 2048

# Create a certificate signing request (CSR) for the broker
openssl req -new -key config/mosquitto/certs/broker.key \
  -out config/mosquitto/certs/broker.csr \
  -subj "/C=BR/ST=Minas Gerais/L=Belo Horizonte/O=UFMG/OU=DELT/CN=localhost" \
  -config "$OPENSSL_CONFIG"

# Sign the broker's CSR with the CA to create the broker certificate
openssl x509 -req -in config/mosquitto/certs/broker.csr \
  -CA config/mosquitto/certs/ca.crt \
  -CAkey config/mosquitto/certs/ca.key -CAcreateserial \
  -out config/mosquitto/certs/broker.crt -days 365 -sha256 \
  -extfile "$OPENSSL_CONFIG" -extensions server_cert

# 4. Create Mosquitto password file
echo "🔒 Creating Mosquitto password file..."
touch config/mosquitto/config/passwordfile

# Use Mosquitto Docker container to generate a password entry
docker run --rm -v "$(pwd)/config/mosquitto/config:/mosquitto" eclipse-mosquitto \
  mosquitto_passwd -b /mosquitto/passwordfile $USER $PASSWORD

# Restrict access to password file
chmod 700 config/mosquitto/config/passwordfile
chown root:root config/mosquitto/config/passwordfile

# 5. Generate mosquitto.conf
echo "⚙️ Writing mosquitto.conf..."
cat > config/mosquitto/config/mosquitto.conf <<EOF
persistence true
persistence_location /mosquitto/data/

log_dest file /mosquitto/log/mosquitto.log

# TLS Listener with client authentication
listener 8883
cafile /mosquitto/config/certs/ca.crt
certfile /mosquitto/config/certs/broker.crt
keyfile /mosquitto/config/certs/broker.key
require_certificate true
use_identity_as_username false

# Authentication
allow_anonymous false
password_file /mosquitto/config/passwordfile
EOF

# 6. Clean up CSR files
echo "🧼 Cleaning up temporary certificate requests (.csr)..."
rm config/mosquitto/certs/*.csr
rm -rf config/mosquitto/config/data
rm -rf config/mosquitto/config/log
rm -rf config/mosquitto/config/certs

# 7. Set permissions
chmod -R a+rw config/mosquitto/log
chmod -R a+rw config/mosquitto/data
chmod -R 755 config/mosquitto
chmod 644 config/mosquitto/certs/*.key
chmod 644 config/mosquitto/certs/*.crt

echo "✅ Mosquitto TLS+Auth secure setup complete!"
