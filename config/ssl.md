# Mosquitto Secure Configuration Structure

This project sets up a secure Mosquitto MQTT broker using **TLS encryption** and **username/password authentication**. Certificates are generated using a local Certificate Authority (CA) and OpenSSL configuration.

---

## 📁 Folder Structure
```
config/
├── mosquitto
│ ├── certs
│ │ ├── broker.crt # Broker (server) TLS certificate signed by the CA
│ │ ├── broker.key # Private key for broker.crt
│ │ ├── ca.crt # Root CA certificate (shared with clients)
│ │ ├── ca.key # Root CA private key (KEEP SECRET)
│ │ ├── ca.srl # Serial number file used for certificate issuance
│ │ ├── client.crt # Client TLS certificate signed by the CA
│ │ ├── client.key # Private key for client.crt
│ │ └── client.pem # Combined client certificate + key (used by Paho clients)
│ ├── config
│ │ ├── mosquitto.conf # Mosquitto configuration with TLS and authentication
│ │ └── passwordfile # Mosquitto password file (username/password credentials)
│ ├── data # Persistence data directory (Mosquitto writes here)
│ └── log # Mosquitto log output directory
└── openssl.cnf # OpenSSL configuration for generating CA, server, and client certificates
```

## 🔐 Certificate Explanation

| File | Description |
|------|-------------|
| `ca.key` | Private key of your custom Certificate Authority (CA). **Do not share this.** |
| `ca.crt` | Public certificate for the CA. Distributed to brokers and clients to verify signatures. |
| `ca.srl` | Serial number tracking file used internally by OpenSSL when issuing certs. |
| `broker.key` | Private key used by the MQTT broker to decrypt TLS sessions. |
| `broker.csr` | Request used to generate the broker certificate, signed by the CA. |
| `broker.crt` | TLS certificate used by the broker to authenticate itself to clients. |
| `client.key` | Private key used by the client to authenticate to the broker. |
| `client.csr` | Request used to generate the client certificate. |
| `client.crt` | TLS certificate used by the client, signed by the CA. |
| `client.pem` | Combined `client.crt` and `client.key` used by some client libraries (e.g., Paho C++). |

---

## ⚙️ Mosquitto Configuration

- `mosquitto.conf`  
  Configures the broker with:
  - TLS listener on port `8883`
  - Enforced client certificate validation (`require_certificate true`)
  - Local password-based authentication (`password_file`)
  - Logging and persistence paths

- `passwordfile`  
  Contains hashed username/password pairs, created with `mosquitto_passwd`.

---

## 📜 OpenSSL Configuration

- `openssl.cnf`  
  Defines:
  - Default certificate parameters (subject fields)
  - Extensions for CA (`v3_ca`), server (`server_cert`), and client (`client_cert`)
  - Subject Alternative Names (SANs), e.g., `localhost` and `ipberryrasp.local`

---

## 🧪 Usage Summary

1. Run `setup_mqtt_secure.sh` to:
   - Generate CA, broker, and client certificates
   - Create the Mosquitto config and password file
2. Use the `config/mosquitto` directory as a Docker volume for Mosquitto
3. Connect clients using:
   - `client.pem` and `ca.crt`
   - MQTT URL like `mqtts://ipberryrasp.local:8883`

---

## ✅ Requirements

- OpenSSL
- Docker (for Mosquitto)
- A valid `openssl.cnf` file
- The script: `setup_mqtt_secure.sh`

---

