#!/usr/bin/env bash
# Self-signed certificate so other devices on the LAN can open the client
# over HTTPS (browsers only allow camera/mic on secure origins).
# Browsers will warn once; accept it. For cross-network demos prefer a
# tunnel (cloudflared / ngrok), which provides a trusted certificate.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p certs
openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -keyout certs/key.pem -out certs/cert.pem \
  -subj "/CN=mini-meet.local"
echo "wrote certs/key.pem and certs/cert.pem; restart the server to use HTTPS"
