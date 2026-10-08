#!/usr/bin/env bash
# Workload de referencia: el test de NullVoxPopuli/disk-perf-git-and-pnpm (git clean + pnpm install),
# primero en serie (comparable con la tabla de su README) y después N copias en paralelo.
# uso: workload.sh <workdir>   (COPIES=4 por default)
# Salida CSV: caso,real_s,user_s,sys_s
set -euo pipefail

W=$1/nvp
REF=7ee1aa4  # commit fijo para que todas las máquinas midan lo mismo
COPIES=${COPIES:-4}
TIMEFORMAT='%R,%U,%S'

if ! command -v pnpm >/dev/null; then
  echo "# sin pnpm en PATH: workload salteado" >&2
  exit 0
fi

if [ ! -d "$W/main" ]; then
  git clone -q https://github.com/NullVoxPopuli/disk-perf-git-and-pnpm "$W/main"
  git -C "$W/main" checkout -q "$REF"
fi
export npm_config_store_dir=$W/store
(cd "$W/main" && pnpm install >/dev/null 2>&1)  # llena el store: después no se toca la red

echo "caso,real_s,user_s,sys_s"
for _ in 1 2 3; do
  printf 'clean,'
  { time (cd "$W/main" && git clean -Xfdq && git clean -fdq); } 2>&1
  printf 'install,'
  { time (cd "$W/main" && pnpm install --offline >/dev/null 2>&1); } 2>&1
done

for i in $(seq "$COPIES"); do
  [ -d "$W/c$i" ] || git -C "$W/main" worktree add -q --detach "$W/c$i"
  (cd "$W/c$i" && git clean -Xfdq)
done
printf 'install_paralelo_x%s,' "$COPIES"
{ time (for i in $(seq "$COPIES"); do (cd "$W/c$i" && pnpm install --offline >/dev/null 2>&1) & done; wait); } 2>&1
printf 'clean_paralelo_x%s,' "$COPIES"
{ time (for i in $(seq "$COPIES"); do (cd "$W/c$i" && git clean -Xfdq) & done; wait); } 2>&1
