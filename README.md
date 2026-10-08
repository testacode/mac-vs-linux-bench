# mac-vs-linux-bench

Batería de pruebas para explicar por qué git, pnpm y los agentes en paralelo andan
mucho más lento en macOS que en Linux **sobre el mismo hardware**.

Punto de partida (2026-10-07, M4 Max): 8 `pnpm install` en paralelo tardan 41–44 s en
macOS y 4,4 s en una VM Linux en la misma Mac. Tanto en un RAM disk sin cifrar como en
HFS+ pasa lo mismo, y el 96% de la CPU se va en kernel. No es el SSD, no es FileVault
y no es APFS en particular: es algo del kernel de macOS que no escala con hilos.

## Hallazgos (M4 Max, macOS 26, 2026-10-08)

1. **Ninguna operación de metadata escala con hilos en macOS.** Con 16 hilos, cada uno
   en su propio directorio, create/rename/mkdir/readdir rinden *menos* que con 1 hilo
   (0,3×–0,7×). En una VM Linux sobre la misma Mac escalan 4–7×.
2. **La causa principal es un lock del b-tree de APFS.** `spindump` durante `create` con
   16 hilos: el 55% de las muestras esperan un `IORWLock` dentro de `bt_lookup_internal`
   y `bt_insert`. Aunque cada hilo trabaje en su directorio, todos pasan por el mismo
   árbol del volumen. HFS+ (probado en RAM disk) da igual o peor.
3. **Las capas de seguridad suman, pero no son la causa.** El hook de sandbox del
   sistema en cada create es ~12% de las muestras y el de procedencia/cuarentena ~8%.
   Si la terminal es una app bajada de internet, cada archivo nuevo se lleva además el
   xattr `com.apple.provenance` (~30% de las muestras de create). Igual, correr desde
   Terminal.app solo mejora el install ~7%.
4. **El SSD y el cifrado no son.** RAM disk sin cifrar ≈ SSD con FileVault; lectura
   random 4k escala 13×.
5. **Los sockets locales tampoco escalan** (ping-pong unix 1,5× contra 16× en Linux): un
   segundo cuello aparte, probablemente cómo se despiertan los hilos.

Workload de NullVoxPopuli: install 49,8 s en macOS contra 17,9 s en la VM; 4 installs
en paralelo, 296 s contra 35 s.

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
