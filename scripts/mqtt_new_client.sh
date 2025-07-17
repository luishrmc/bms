#!/bin/bash

# Usage: ./create_new_client.sh <client_name>
# Example: ./create_new_client.sh telemetry-node-1

CLIENT_NAME="$1"
OPENSSL_CONFIG="config/openssl.cnf"
CA_DIR="config/mosquitto/certs"
CLIENT_DIR="$CA_DIR/clients/$CLIENT_NAME"

if [ -z "$CLIENT_NAME" ]; then
  echo "❌ ERROR: You must provide a client name."
  echo "Usage: $0 <client_name>"
  exit 1
fi

# Check if CA files exist
if [[ ! -f "$CA_DIR/ca.crt" || ! -f "$CA_DIR/ca.key" ]]; then
  echo "❌ ERROR: CA files not found. Run setup_mqtt_secure.sh first."
  exit 1
fi

# Create client cert directory
echo "📁 Creating directory: $CLIENT_DIR"
mkdir -p "$CLIENT_DIR"

# 1. Generate private key
echo "🔑 Generating private key for $CLIENT_NAME..."
openssl genrsa -out "$CLIENT_DIR/$CLIENT_NAME.key" 2048

# 2. Create CSR
echo "📄 Creating CSR..."
openssl req -new -key "$CLIENT_DIR/$CLIENT_NAME.key" \
  -out "$CLIENT_DIR/$CLIENT_NAME.csr" \
  -subj "/C=BR/ST=Minas Gerais/L=Belo Horizonte/O=UFMG/OU=DELT/CN=$CLIENT_NAME" \
  -config "$OPENSSL_CONFIG"

# 3. Sign CSR with CA to get client certificate
echo "✅ Signing certificate with CA..."
openssl x509 -req -in "$CLIENT_DIR/$CLIENT_NAME.csr" \
  -CA "$CA_DIR/ca.crt" -CAkey "$CA_DIR/ca.key" -CAcreateserial \
  -out "$CLIENT_DIR/$CLIENT_NAME.crt" -days 365 -sha256 \
  -extfile "$OPENSSL_CONFIG" -extensions client_cert

# 4. Generate PEM bundle
echo "📦 Creating PEM bundle..."
cat "$CLIENT_DIR/$CLIENT_NAME.crt" "$CLIENT_DIR/$CLIENT_NAME.key" > "$CLIENT_DIR/$CLIENT_NAME.pem"

# 5. Clean up CSR
rm "$CLIENT_DIR/$CLIENT_NAME.csr"

# 6. Copy the CA certificate to the client directory
echo "📄 Copying CA certificate to client directory..."
cp "$CA_DIR/ca.crt" "$CLIENT_DIR/ca.crt"

# 7. Set permissions
chmod 644 "$CLIENT_DIR"/*

echo "✅ Client certificate generated successfully!"
echo "🔐 Files created in: $CLIENT_DIR"
echo "   - $CLIENT_NAME.key     (Private key)"
echo "   - $CLIENT_NAME.crt     (Signed certificate)"
echo "   - $CLIENT_NAME.pem     (Combined cert + key for some clients)"
echo "   - ca.crt               (CA certificate)"
