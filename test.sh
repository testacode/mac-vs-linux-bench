#!/usr/bin/env bash
# Smoke test: compila sin warnings, cada grupo emite CSV bien formado y no deja basura.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
WORK=$ROOT/.work
mkdir -p "$WORK"
cc -O2 -Wall -Wextra -Werror -pthread -o "$WORK/micro" "$ROOT/micro.c"

for g in fs sys spawn sock io; do
  dir=$WORK/test-$$-$g
  out=$("$WORK/micro" "$g" 2 50 "$dir")
  echo "$out" | awk -F, -v g="$g" '
    NF != 8 || $1 != g || $3 != 2 || $4 <= 0 { print "fila inválida: " $0; bad = 1 }
    END { exit bad }'
  [ ! -e "$dir" ] || { echo "$g dejó $dir sin limpiar"; exit 1; }
  echo "ok $g ($(echo "$out" | wc -l | tr -d ' ') fases)"
done

if "$WORK/micro" nope 1 50 "$WORK/x" 2>/dev/null; then echo "grupo inválido no falló"; exit 1; fi
echo "ok argumentos inválidos"

printf 'grupo,fase,hilos,ops,seg,ops_s,user_s,sys_s\nfs,stat,1,10,1,1000,0,0.5\nfs,stat,4,40,1,4000,0,1\n' > "$WORK/test-$$.csv"
mkdir -p "$WORK/test-$$-run" && mv "$WORK/test-$$.csv" "$WORK/test-$$-run/micro.csv"
python3 -I "$ROOT/compare.py" "$WORK/test-$$-run" | grep -q '| fs/stat | 1k → 4k | 4.0x | 100% |' || { echo "compare.py cambió de salida"; exit 1; }
echo "ok compare.py"
