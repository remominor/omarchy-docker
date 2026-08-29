#!/usr/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  cp .env.example .env
  echo "Created .env. Edit NVIDIA_VISIBLE_DEVICES, OMARCHY_IP and DOCKER_NETWORK, then rerun." >&2
  exit 2
fi

docker compose -f compose-unraid-ip.yml build
docker compose -f compose-unraid-ip.yml up -d

echo
echo "Container started. Next:"
echo "  docker logs -f omarchy"
echo "  docker exec -it omarchy omarchy-container-check"
