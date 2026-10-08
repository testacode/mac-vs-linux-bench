#!/usr/bin/env bash
# La misma batería dentro de la VM Linux de Docker Desktop, sobre un volumen ext4 nativo de la VM.
# Mismo hardware, otro kernel: aísla qué parte de la lentitud es de macOS.
# uso: ./run-linux.sh [label]   (acepta las mismas variables que run.sh)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
LABEL=${1:-$(hostname -s)-linuxvm}
VOL=mac-vs-linux-bench-work

docker volume create "$VOL" >/dev/null
docker run --rm \
  -v "$VOL:/work" -v "$ROOT:/bench" \
  -e WORKDIR=/work -e QUICK="${QUICK:-0}" -e THREADS="${THREADS:-1 2 4 8 16}" \
  -e SKIP_WORKLOAD="${SKIP_WORKLOAD:-0}" -e COREPACK_ENABLE_DOWNLOAD_PROMPT=0 \
  node:22 bash -c 'corepack enable && /bench/run.sh "$0"' "$LABEL"
docker volume rm "$VOL" >/dev/null
