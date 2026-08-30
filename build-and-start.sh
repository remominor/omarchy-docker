#!/usr/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  cp .env.example .env
  echo "Created .env. Edit NVIDIA_VISIBLE_DEVICES and APPDATA_PATH, then rerun." >&2
  exit 2
fi

docker compose -f compose-cachyos.yml build
docker compose -f compose-cachyos.yml up -d

echo
echo "Container started. Next:"
echo "  docker logs -f omarchy"
echo "  docker exec -it omarchy omarchy-container-check"
