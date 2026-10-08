#!/usr/bin/env bash
# Corre la batería completa en esta máquina (macOS nativo o Linux) y deja resultados en results/<label>/.
# uso: ./run.sh [label]
#   QUICK=1          corrida corta para probar que todo anda
#   WORKDIR=<dir>    volumen a medir (default: .work/ dentro del repo)
#   THREADS="1 4 16" hilos a probar
#   SKIP_WORKLOAD=1  saltea el workload de pnpm (no necesita node/pnpm)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
LABEL=${1:-$(hostname -s)-$(uname -s | tr '[:upper:]' '[:lower:]')}
WORK=${WORKDIR:-$ROOT/.work}
OUT=$ROOT/results/$LABEL
N=2000
THREADS=${THREADS:-"1 2 4 8 16"}
if [ "${QUICK:-0}" = 1 ]; then N=200; THREADS="1 4"; fi

mkdir -p "$OUT" "$WORK"
cc -O2 -pthread -o "$WORK/micro" "$ROOT/micro.c"

echo "→ inventario"
"$ROOT/inventory.sh" "$WORK" > "$OUT/inventory.txt" 2>&1 || true

echo "→ microbenchmarks (n=$N, hilos: $THREADS)"
CSV=$OUT/micro.csv
echo "grupo,fase,hilos,ops,seg,ops_s,user_s,sys_s" > "$CSV"
for g in fs sys spawn sock io; do
  for t in $THREADS; do
    "$WORK/micro" "$g" "$t" "$N" "$WORK/m-$$-$g-$t" >> "$CSV"
  done
  echo "  $g ok"
done

if [ "${SKIP_WORKLOAD:-0}" != 1 ]; then
  echo "→ workload pnpm (NullVoxPopuli/disk-perf-git-and-pnpm)"
  "$ROOT/workload.sh" "$WORK" > "$OUT/workload.csv"
fi
echo "listo: $OUT"
