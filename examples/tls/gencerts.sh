#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Paths
CA_KEY=ca-key.pem
CA_CERT=ca-cert.pem
SRV_KEY=server-key.pem
SRV_CSR=server.csr
SRV_CERT=server-cert.pem
CLI_KEY=client-key.pem
CLI_CSR=client.csr
CLI_CERT=client-cert.pem

if ! command -v openssl >/dev/null 2>&1; then
  echo "ERROR: openssl is required but not found in PATH" >&2
  exit 1
fi

echo "Generating CA key and certificate..."
openssl genrsa -out "$CA_KEY" 4096 1>/dev/null 2>&1
openssl req -x509 -new -nodes -sha256 -days 3650 \
  -key "$CA_KEY" -out "$CA_CERT" -subj "/CN=Local Test CA" 1>/dev/null 2>&1

echo "Generating server key and CSR..."
openssl genrsa -out "$SRV_KEY" 2048 1>/dev/null 2>&1
openssl req -new -key "$SRV_KEY" -out "$SRV_CSR" -subj "/CN=localhost" \
  -addext "subjectAltName = DNS:localhost,IP:127.0.0.1" 1>/dev/null 2>&1

echo "Signing server certificate with CA..."
openssl x509 -req -in "$SRV_CSR" -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
  -out "$SRV_CERT" -days 825 -sha256 -extfile <(printf "subjectAltName=DNS:localhost,IP:127.0.0.1") 1>/dev/null 2>&1
rm -f "$SRV_CSR"

echo "Generating client key and CSR..."
openssl genrsa -out "$CLI_KEY" 2048 1>/dev/null 2>&1
openssl req -new -key "$CLI_KEY" -out "$CLI_CSR" -subj "/CN=Local Test Client" 1>/dev/null 2>&1

echo "Signing client certificate with CA..."
openssl x509 -req -in "$CLI_CSR" -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
  -out "$CLI_CERT" -days 825 -sha256 1>/dev/null 2>&1
rm -f "$CLI_CSR"

echo "Done. Generated files:"
ls -l "$CA_CERT" "$CA_KEY" "$SRV_CERT" "$SRV_KEY" "$CLI_CERT" "$CLI_KEY"

# Restrict key permissions (best practice)
chmod 600 "$CA_KEY" "$SRV_KEY" "$CLI_KEY"
