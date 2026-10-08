# mac-vs-linux-bench

Batería de pruebas para explicar por qué git, pnpm y los agentes en paralelo andan
mucho más lento en macOS que en Linux **sobre el mismo hardware**.

Punto de partida (2026-10-07, M4 Max): 8 `pnpm install` en paralelo tardan 41–44 s en
macOS y 4,4 s en una VM Linux en la misma Mac. Tanto en un RAM disk sin cifrar como en
HFS+ pasa lo mismo, y el 96% de la CPU se va en kernel. No es el SSD, no es FileVault
y no es APFS en particular: es algo del kernel de macOS que no escala con hilos.

## Qué mide

| Grupo | Fases | Para descartar / confirmar |
|---|---|---|
| `fs` | create, stat, open+read, readdir, rename, link, clonefile, unlink, mkdir, rmdir | qué operación de metadata no escala |
| `sys` | getppid, stat de un mismo archivo desde todos los hilos | costo de syscall pelada y de un vnode compartido |
| `spawn` | posix_spawn + wait | lanzar procesos (pnpm/git/node lanzan muchos) |
| `sock` | ping-pong unix y TCP loopback, connect+accept | red local |
| `io` | escritura secuencial y lectura random 4k sin caché | control: el SSD en sí |
| workload | test de [NullVoxPopuli/disk-perf-git-and-pnpm](https://github.com/NullVoxPopuli/disk-perf-git-and-pnpm) en serie y 4 copias en paralelo | comparable con su tabla de ~70 máquinas |

Cada fase corre con 1, 2, 4, 8 y 16 hilos y registra reloj, CPU user y CPU sys.
`inventory.sh` anota lo que puede meterse en el medio: FileVault, SIP, Spotlight, MDM,
system extensions (EDR, antivirus, firewall) y kexts.

## Cómo correrlo

Requisitos: `cc` (Xcode CLT) y `git`. Para el workload: `node` ≥ 22 y `pnpm` ≥ 10.
Para la comparación con Linux: Docker Desktop abierto.

```bash
./run.sh                    # macOS nativo → results/<host>-darwin/
./run-linux.sh              # misma batería en la VM Linux de Docker → results/<host>-linuxvm/
./compare.py results/*      # tabla comparativa
sudo ./profile.sh           # opcional: stacks de kernel con spindump durante la fase fs
./test.sh                   # smoke test del propio benchmark
```

`QUICK=1` hace una corrida corta, `SKIP_WORKLOAD=1` saltea pnpm y `WORKDIR=/Volumes/X/w`
mide otro volumen.

## Privacidad

`results/` está en `.gitignore`. En una máquina corporativa, el inventario lista el
software de seguridad instalado: revisalo antes de compartirlo.
