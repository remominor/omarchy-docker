#!/usr/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  cp .env.example .env
  echo "Created .env. Edit NVIDIA_VISIBLE_DEVICES and APPDATA_PATH, then rerun." >&2
  exit 2
fi

template="templates/compose.${OMARCHY_TEMPLATE:-bridge}.yml"
if [[ ! -f "$template" ]]; then
  echo "Unknown OMARCHY_TEMPLATE=${OMARCHY_TEMPLATE:-bridge}; use headless, bridge, or seat9" >&2
  exit 2
fi

docker compose -f "$template" build
docker compose -f "$template" up -d

echo
echo "Container started. Next:"
echo "  docker logs -f omarchy"
echo "  docker exec -it omarchy omarchy-container-check"
