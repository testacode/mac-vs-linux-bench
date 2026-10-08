#!/usr/bin/env bash
# Muestrea los stacks de kernel con spindump mientras un grupo de micro corre con 16 hilos,
# para ver en qué lock/función se va el tiempo de sys. Solo macOS, requiere root.
# uso: sudo ./profile.sh [label] [grupo]   (grupo: fs por default, o sys|spawn|sock|io)
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "correr con sudo" >&2; exit 1; }
ROOT=$(cd "$(dirname "$0")" && pwd)
LABEL=${1:-$(hostname -s)-$(uname -s | tr '[:upper:]' '[:lower:]')}
GROUP=${2:-fs}
WORK=${WORKDIR:-$ROOT/.work}
OUT=$ROOT/results/$LABEL
mkdir -p "$OUT" "$WORK"
cc -O2 -pthread -o "$WORK/micro" "$ROOT/micro.c"

"$WORK/micro" "$GROUP" 16 20000 "$WORK/prof-$$" > "$OUT/profile-micro-$GROUP.csv" &
pid=$!
spindump "$pid" 10 10 -o "$OUT/spindump-$GROUP.txt"
wait "$pid"
chown -R "${SUDO_USER:-$(id -un)}" "$OUT" "$WORK"
echo "listo: $OUT/spindump-$GROUP.txt"
