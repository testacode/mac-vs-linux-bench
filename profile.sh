#!/usr/bin/env bash
# Muestrea los stacks de kernel con spindump mientras corre la fase fs con 16 hilos,
# para ver en qué lock/función se va el tiempo de sys. Solo macOS, requiere root.
# uso: sudo ./profile.sh [label]
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "correr con sudo" >&2; exit 1; }
ROOT=$(cd "$(dirname "$0")" && pwd)
LABEL=${1:-$(hostname -s)-$(uname -s | tr '[:upper:]' '[:lower:]')}
WORK=${WORKDIR:-$ROOT/.work}
OUT=$ROOT/results/$LABEL
mkdir -p "$OUT" "$WORK"
cc -O2 -pthread -o "$WORK/micro" "$ROOT/micro.c"

"$WORK/micro" fs 16 20000 "$WORK/prof-$$" > "$OUT/profile-micro.csv" &
pid=$!
spindump "$pid" 10 10 -o "$OUT/spindump.txt"
wait "$pid"
chown -R "${SUDO_USER:-$(id -un)}" "$OUT" "$WORK"
echo "listo: $OUT/spindump.txt"
