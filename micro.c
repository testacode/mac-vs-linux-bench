// micro.c — microbenchmarks del kernel: archivos, syscalls, procesos, sockets e I/O.
// Cada fase corre en N hilos a la vez; se mide reloj y CPU (user/sys) del proceso entero.
// Uso: micro <fs|sys|spawn|sock|io> <hilos> <n> <dir>
// Salida CSV (sin header): grupo,fase,hilos,ops,seg,ops_s,user_s,sys_s
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

extern char **environ;

#define DIE(msg) do { perror(msg); exit(1); } while (0)
#define IO_MB 64
#define BLK (1 << 20)

typedef struct { const char *name; long (*fn)(int id); } phase_t;

static const char *grp, *base;
static int nthreads, n;
static const phase_t *phases;
static int nphases;

// Barrera casera: macOS no tiene pthread_barrier_t. Participan los hilos + main.
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int waiting, generation;

static void barrier(void) {
  pthread_mutex_lock(&mu);
  int gen = generation;
  if (++waiting == nthreads + 1) {
    waiting = 0;
    generation++;
    pthread_cond_broadcast(&cv);
  } else {
    while (gen == generation) pthread_cond_wait(&cv, &mu);
  }
  pthread_mutex_unlock(&mu);
}

static void fpath(char *buf, int id, int i, const char *suf) {
  snprintf(buf, PATH_MAX, "%s/t%d/f%d%s", base, id, i, suf);
}

static void dpath(char *buf, int id, int i) {
  snprintf(buf, PATH_MAX, "%s/t%d/d%d", base, id, i);
}

// ---- fs: operaciones de metadata, cada hilo en su propio directorio ----

static long fs_create(int id) {
  char p[PATH_MAX], data[100] = {0};
  for (int i = 0; i < n; i++) {
    fpath(p, id, i, "");
    int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0 || write(fd, data, sizeof data) != sizeof data) DIE("create");
    close(fd);
  }
  return n;
}

static long fs_stat(int id) {
  char p[PATH_MAX];
  struct stat st;
  for (int i = 0; i < n; i++) {
    fpath(p, id, i, "");
    if (stat(p, &st)) DIE("stat");
  }
  return n;
}

static long fs_read(int id) {
  char p[PATH_MAX], buf[100];
  for (int i = 0; i < n; i++) {
    fpath(p, id, i, "");
    int fd = open(p, O_RDONLY);
    if (fd < 0 || read(fd, buf, sizeof buf) < 0) DIE("read");
    close(fd);
  }
  return n;
}

static long fs_readdir(int id) {
  char p[PATH_MAX];
  long entries = 0;
  snprintf(p, PATH_MAX, "%s/t%d", base, id);
  for (int r = 0; r < 10; r++) {
    DIR *d = opendir(p);
    if (!d) DIE("opendir");
    while (readdir(d)) entries++;
    closedir(d);
  }
  return entries;
}

static long fs_rename(int id) {
  char a[PATH_MAX], b[PATH_MAX];
  for (int i = 0; i < n; i++) {
    fpath(a, id, i, "");
    fpath(b, id, i, ".r");
    if (rename(a, b) || rename(b, a)) DIE("rename");
  }
  return 2L * n;
}

static long fs_link(int id) {
  char a[PATH_MAX], b[PATH_MAX];
  for (int i = 0; i < n; i++) {
    fpath(a, id, i, "");
    fpath(b, id, i, ".l");
    if (link(a, b) || unlink(b)) DIE("link");
  }
  return 2L * n;
}

#ifdef __APPLE__
static long fs_clone(int id) {
  char a[PATH_MAX], b[PATH_MAX];
  for (int i = 0; i < n; i++) {
    fpath(a, id, i, "");
    fpath(b, id, i, ".c");
    if (clonefile(a, b, 0) || unlink(b)) DIE("clonefile");
  }
  return 2L * n;
}
#endif

static long fs_unlink(int id) {
  char p[PATH_MAX];
  for (int i = 0; i < n; i++) {
    fpath(p, id, i, "");
    if (unlink(p)) DIE("unlink");
  }
  return n;
}

static long fs_mkdir(int id) {
  char p[PATH_MAX];
  for (int i = 0; i < n; i++) {
    dpath(p, id, i);
    if (mkdir(p, 0755)) DIE("mkdir");
  }
  return n;
}

static long fs_rmdir(int id) {
  char p[PATH_MAX];
  for (int i = 0; i < n; i++) {
    dpath(p, id, i);
    if (rmdir(p)) DIE("rmdir");
  }
  return n;
}

static const phase_t FS[] = {
  {"create", fs_create}, {"stat", fs_stat}, {"open_read", fs_read},
  {"readdir", fs_readdir}, {"rename_x2", fs_rename}, {"link+unlink", fs_link},
#ifdef __APPLE__
  {"clonefile+unlink", fs_clone},
#endif
  {"unlink", fs_unlink}, {"mkdir", fs_mkdir}, {"rmdir", fs_rmdir},
};

// ---- sys: costo de syscall pelada y de pegarle todos al mismo vnode ----

static long sys_getppid(int id) {
  (void)id;
  long k = 100L * n;
  for (long i = 0; i < k; i++) getppid();
  return k;
}

static long sys_stat_shared(int id) {
  (void)id;
  char p[PATH_MAX];
  struct stat st;
  snprintf(p, PATH_MAX, "%s/shared", base);
  for (int i = 0; i < 10 * n; i++)
    if (stat(p, &st)) DIE("stat shared");
  return 10L * n;
}

static const phase_t SYS[] = {{"getppid", sys_getppid}, {"stat_mismo_archivo", sys_stat_shared}};

// ---- spawn: lanzar procesos (pnpm, git y node lanzan muchísimos) ----

static long spawn_true(int id) {
  (void)id;
  char *argv[] = {"true", NULL};
  int k = n / 10;
  for (int i = 0; i < k; i++) {
    pid_t pid;
    int st;
    if (posix_spawn(&pid, "/usr/bin/true", NULL, NULL, argv, environ)) DIE("posix_spawn");
    waitpid(pid, &st, 0);
  }
  return k;
}

static const phase_t SPAWN[] = {{"posix_spawn+wait", spawn_true}};

// ---- sock: ping-pong de 1 byte contra un hilo eco, y abrir/cerrar conexiones ----

static void *echo(void *arg) {
  int fd = (int)(intptr_t)arg;
  char c;
  while (read(fd, &c, 1) == 1)
    if (write(fd, &c, 1) != 1) break;
  return NULL;
}

static long pingpong(int fd, int peer) {
  pthread_t t;
  char c = 'x';
  long k = 5L * n;
  pthread_create(&t, NULL, echo, (void *)(intptr_t)peer);
  for (long i = 0; i < k; i++)
    if (write(fd, &c, 1) != 1 || read(fd, &c, 1) != 1) DIE("pingpong");
  close(fd);
  pthread_join(t, NULL);
  close(peer);
  return k;
}

static int tcp_listener(struct sockaddr_in *addr) {
  socklen_t len = sizeof *addr;
  int lfd = socket(AF_INET, SOCK_STREAM, 0);
  memset(addr, 0, sizeof *addr);
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(lfd, (struct sockaddr *)addr, sizeof *addr) || listen(lfd, 16) ||
      getsockname(lfd, (struct sockaddr *)addr, &len))
    DIE("listen");
  return lfd;
}

// connect bloqueante a loopback vuelve antes del accept: el kernel completa el handshake solo.
static void tcp_pair(int lfd, struct sockaddr_in *addr, int *cfd, int *sfd) {
  int one = 1;
  *cfd = socket(AF_INET, SOCK_STREAM, 0);
  if (connect(*cfd, (struct sockaddr *)addr, sizeof *addr)) DIE("connect");
  if ((*sfd = accept(lfd, NULL, NULL)) < 0) DIE("accept");
  setsockopt(*cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  setsockopt(*sfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

static long sock_unix(int id) {
  (void)id;
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) DIE("socketpair");
  return pingpong(sv[0], sv[1]);
}

static long sock_tcp(int id) {
  (void)id;
  struct sockaddr_in addr;
  int lfd = tcp_listener(&addr), cfd, sfd;
  tcp_pair(lfd, &addr, &cfd, &sfd);
  close(lfd);
  return pingpong(cfd, sfd);
}

// Cierra con RST (linger 0): sin TIME_WAIT, 16 hilos no agotan los ~16k puertos efímeros de macOS.
static long sock_tcp_connect(int id) {
  (void)id;
  struct sockaddr_in addr;
  struct linger rst = {1, 0};
  int lfd = tcp_listener(&addr), k = n / 2;
  for (int i = 0; i < k; i++) {
    int cfd, sfd;
    tcp_pair(lfd, &addr, &cfd, &sfd);
    setsockopt(cfd, SOL_SOCKET, SO_LINGER, &rst, sizeof rst);
    close(cfd);
    close(sfd);
  }
  close(lfd);
  return k;
}

static const phase_t SOCK[] = {
  {"unix_pingpong", sock_unix}, {"tcp_pingpong", sock_tcp}, {"tcp_connect+accept", sock_tcp_connect},
};

// ---- io: lectura/escritura de datos esquivando la caché (control: el SSD en sí) ----

static int open_direct(const char *p, int flags) {
#ifdef __APPLE__
  int fd = open(p, flags, 0644);
  if (fd >= 0) fcntl(fd, F_NOCACHE, 1);
#else
  int fd = open(p, flags | O_DIRECT, 0644);
#endif
  if (fd < 0) DIE("open io");
  return fd;
}

static void *aligned_buf(void) {
  void *buf;
  if (posix_memalign(&buf, 4096, BLK)) DIE("posix_memalign");
  memset(buf, 'x', BLK);
  return buf;
}

static long io_seqwrite(int id) {
  char p[PATH_MAX];
  void *buf = aligned_buf();
  snprintf(p, PATH_MAX, "%s/t%d/io", base, id);
  int fd = open_direct(p, O_CREAT | O_WRONLY | O_TRUNC);
  for (int i = 0; i < IO_MB; i++)
    if (write(fd, buf, BLK) != BLK) DIE("seqwrite");
  fsync(fd);
  close(fd);
  free(buf);
  return IO_MB;
}

static long io_randread(int id) {
  char p[PATH_MAX];
  void *buf = aligned_buf();
  unsigned seed = (unsigned)id + 1;
  long blocks = (long)IO_MB * BLK / 4096, k = 5L * n;
  snprintf(p, PATH_MAX, "%s/t%d/io", base, id);
  int fd = open_direct(p, O_RDONLY);
  for (long i = 0; i < k; i++)
    if (pread(fd, buf, 4096, (off_t)(rand_r(&seed) % blocks) * 4096) != 4096) DIE("randread");
  close(fd);
  free(buf);
  return k;
}

static const phase_t IO[] = {{"seqwrite_MB", io_seqwrite}, {"randread_4k", io_randread}};

// ---- driver ----

static long *ops_by_thread;

static void *worker(void *arg) {
  int id = (int)(intptr_t)arg;
  for (int ph = 0; ph < nphases; ph++) {
    barrier();
    ops_by_thread[id] = phases[ph].fn(id);
    barrier();
  }
  return NULL;
}

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double tv(struct timeval t) { return t.tv_sec + t.tv_usec / 1e6; }

// CPU del proceso (todos sus hilos) + la de los hijos ya esperados (spawn).
static void cpu(double *user, double *sys) {
  struct rusage self, kids;
  getrusage(RUSAGE_SELF, &self);
  getrusage(RUSAGE_CHILDREN, &kids);
  *user = tv(self.ru_utime) + tv(kids.ru_utime);
  *sys = tv(self.ru_stime) + tv(kids.ru_stime);
}

#define GROUP(name, arr) {name, arr, sizeof arr / sizeof *arr}

static const struct { const char *name; const phase_t *phases; int n; } GROUPS[] = {
  GROUP("fs", FS), GROUP("sys", SYS), GROUP("spawn", SPAWN), GROUP("sock", SOCK), GROUP("io", IO),
};

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr, "uso: %s <fs|sys|spawn|sock|io> <hilos> <n> <dir>\n", argv[0]);
    return 2;
  }
  grp = argv[1];
  nthreads = atoi(argv[2]);
  n = atoi(argv[3]);
  base = argv[4];
  for (size_t i = 0; i < sizeof GROUPS / sizeof *GROUPS; i++) {
    if (!strcmp(grp, GROUPS[i].name)) {
      phases = GROUPS[i].phases;
      nphases = GROUPS[i].n;
    }
  }
  if (!phases || nthreads < 1 || n < 10) {
    fprintf(stderr, "argumentos inválidos\n");
    return 2;
  }

  char p[PATH_MAX];
  if (mkdir(base, 0755)) DIE(base);
  for (int i = 0; i < nthreads; i++) {
    snprintf(p, PATH_MAX, "%s/t%d", base, i);
    if (mkdir(p, 0755)) DIE("mkdir hilo");
  }
  snprintf(p, PATH_MAX, "%s/shared", base);
  close(open(p, O_CREAT | O_WRONLY, 0644));

  ops_by_thread = calloc(nthreads, sizeof *ops_by_thread);
  pthread_t *th = calloc(nthreads, sizeof *th);
  for (int i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);

  for (int ph = 0; ph < nphases; ph++) {
    double u0, s0, u1, s1, t0, t1;
    cpu(&u0, &s0);
    t0 = now();
    barrier();  // larga la fase
    barrier();  // espera que terminen todos
    t1 = now();
    cpu(&u1, &s1);
    long ops = 0;
    for (int i = 0; i < nthreads; i++) ops += ops_by_thread[i];
    printf("%s,%s,%d,%ld,%.4f,%.0f,%.3f,%.3f\n", grp, phases[ph].name, nthreads, ops, t1 - t0,
           ops / (t1 - t0), u1 - u0, s1 - s0);
    fflush(stdout);
  }
  for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);

  // Limpia lo que creó: las fases de fs ya dejan los directorios vacíos.
  for (int i = 0; i < nthreads; i++) {
    snprintf(p, PATH_MAX, "%s/t%d/io", base, i);
    unlink(p);
    snprintf(p, PATH_MAX, "%s/t%d", base, i);
    rmdir(p);
  }
  snprintf(p, PATH_MAX, "%s/shared", base);
  unlink(p);
  rmdir(base);
  return 0;
}
