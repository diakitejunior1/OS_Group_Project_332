#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PIDFILE "/tmp/processgroup.pids"
#define TEMP_PIDFILE "/tmp/processgroup.pids.tmp"
#define MAX_PIDS 4096
#define LINE_BUFSZ 256

/* Global shutdown flag set by signal handler */
static volatile sig_atomic_t shutdown_requested = 0;

/* Simple logger for errors and info */
static void log_error(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "[ERROR] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
static void log_info(const char *fmt, ...) {
    va_list ap;
    fprintf(stdout, "[INFO] ");
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

/* Signal handler */
static void handle_signal(int sig) {
    shutdown_requested = 1;
    /* Do minimal work in handler */
}

/* Ensure pidfile exists; create if missing */
static int ensure_pidfile_exists(void) {
    int fd = open(PIDFILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        log_error("Cannot open/create PID file '%s': %s", PIDFILE, strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

/* Validate pid: returns true if pid exists (we can signal it) */
static bool validate_pid(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) == 0) return true;
    if (errno == EPERM) return true; /* process exists but we lack permission */
    return false;
}

/* Read PIDs under file lock. Caller owns returned count and list. */
static int read_pids(pid_t *out_list, size_t max) {
    if (ensure_pidfile_exists() < 0) return -1;
    int fd = open(PIDFILE, O_RDONLY);
    if (fd < 0) {
        log_error("Failed to open PID file '%s' for reading: %s", PIDFILE, strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_SH) != 0) {
        log_error("Failed to acquire shared lock on '%s': %s", PIDFILE, strerror(errno));
        close(fd);
        return -1;
    }

    FILE *f = fdopen(fd, "r");
    if (!f) {
        log_error("fdopen failed on '%s': %s", PIDFILE, strerror(errno));
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    size_t count = 0;
    char line[LINE_BUFSZ];
    while (fgets(line, sizeof(line), f) && count < max) {
        char *endptr = NULL;
        errno = 0;
        long val = strtol(line, &endptr, 10);
        if (errno != 0 || endptr == line) {
            /* skip malformed lines but warn */
            log_error("Malformed line in PID file skipped: '%s'", line);
            continue;
        }
        if (val <= 0) continue;
        out_list[count++] = (pid_t)val;
    }

    /* Unlock and close via fclose */
    if (fclose(f) != 0) {
        log_error("fclose failed on '%s': %s", PIDFILE, strerror(errno));
        return -1;
    }

    /* flock unlocked by close; fd closed by fclose */
    return (int)count;
}

/* Write PIDs atomically using temp file + rename under exclusive lock on PIDFILE.
   Input list must be of size count. */
static int write_pids_atomic(pid_t *list, size_t count) {
    /* Open temp file for writing */
    int tmpfd = open(TEMP_PIDFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tmpfd < 0) {
        log_error("Cannot open temp pidfile '%s': %s", TEMP_PIDFILE, strerror(errno));
        return -1;
    }
    FILE *f = fdopen(tmpfd, "w");
    if (!f) {
        log_error("fdopen on temp pidfile failed: %s", strerror(errno));
        close(tmpfd);
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (fprintf(f, "%d\n", (int)list[i]) < 0) {
            log_error("Failed to write to temp pidfile: %s", strerror(errno));
            fclose(f);
            unlink(TEMP_PIDFILE);
            return -1;
        }
    }
    if (fflush(f) != 0) {
        log_error("fflush failed on temp pidfile: %s", strerror(errno));
        fclose(f);
        unlink(TEMP_PIDFILE);
        return -1;
    }
    if (fsync(fileno(f)) != 0) {
        log_error("fsync failed on temp pidfile: %s", strerror(errno));
        /* Not fatal but warn */
    }
    if (fclose(f) != 0) {
        log_error("fclose failed on temp pidfile: %s", strerror(errno));
        unlink(TEMP_PIDFILE);
        return -1;
    }

    /* Acquire exclusive lock on original file before rename to reduce race (not perfect, but helpful).
       We'll open PIDFILE (create if missing) and flock it exclusive. */
    int fd = open(PIDFILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        log_error("Cannot open PID file for locking '%s': %s", PIDFILE, strerror(errno));
        unlink(TEMP_PIDFILE);
        return -1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        log_error("Failed to acquire exclusive lock on '%s': %s", PIDFILE, strerror(errno));
        close(fd);
        unlink(TEMP_PIDFILE);
        return -1;
    }

    /* rename temp -> PIDFILE (atomic on same filesystem) */
    if (rename(TEMP_PIDFILE, PIDFILE) != 0) {
        log_error("Failed to rename temp pidfile to '%s': %s", PIDFILE, strerror(errno));
        flock(fd, LOCK_UN);
        close(fd);
        unlink(TEMP_PIDFILE);
        return -1;
    }

    /* Release lock and close */
    if (flock(fd, LOCK_UN) != 0) {
        log_error("Failed to release lock on '%s': %s", PIDFILE, strerror(errno));
        /* continue */
    }
    close(fd);
    return 0;
}

/* Add a PID to the list (with checks). Returns 0 on success, -1 on error. */
static int add_pid(pid_t pid) {
    if (pid <= 0) {
        log_error("PID must be a positive integer.");
        return -1;
    }

    if (!validate_pid(pid)) {
        log_error("PID %d does not exist or cannot be validated.", (int)pid);
        return -1;
    }

    pid_t list[MAX_PIDS];
    int count = read_pids(list, MAX_PIDS);
    if (count < 0) return -1;

    /* check duplicates */
    for (int i = 0; i < count; ++i) {
        if (list[i] == pid) {
            log_info("PID %d already in group (no change).", (int)pid);
            return 0;
        }
    }
    if ((size_t)count >= MAX_PIDS) {
        log_error("PID list is full (limit %d).", MAX_PIDS);
        return -1;
    }

    list[count++] = pid;
    if (write_pids_atomic(list, (size_t)count) != 0) {
        log_error("Failed to persist PID list.");
        return -1;
    }
    log_info("Added PID %d to group.", (int)pid);
    return 0;
}

/* List PIDs */
static int list_pids_cmd(void) {
    pid_t list[MAX_PIDS];
    int count = read_pids(list, MAX_PIDS);
    if (count < 0) return -1;
    printf("Process group (%d):\n", count);
    for (int i = 0; i < count; ++i) {
        printf("  %d\n", (int)list[i]);
    }
    return 0;
}

/* Send signal to all; if deliver_on_missing==false, skip missing pids; if true, treat missing as error */
static int send_signal_all(int sig, bool treat_missing_as_error) {
    pid_t list[MAX_PIDS];
    int count = read_pids(list, MAX_PIDS);
    if (count < 0) return -1;
    int errors = 0;
    for (int i = 0; i < count; ++i) {
        if (shutdown_requested) {
            log_info("Shutdown requested, stopping signal sends.");
            break;
        }
        if (kill(list[i], sig) != 0) {
            if (errno == ESRCH) {
                log_error("PID %d does not exist (skipped).", (int)list[i]);
                errors++;
                continue;
            } else {
                log_error("Failed to send signal %d to PID %d: %s", sig, (int)list[i], strerror(errno));
                errors++;
                continue;
            }
        } else {
            log_info("Sent signal %d to PID %d", sig, (int)list[i]);
        }
    }
    return (errors == 0) ? 0 : -1;
}

/* Show basic resources from /proc/<pid>/status, select a few lines */
static int show_resources_cmd(void) {
    pid_t list[MAX_PIDS];
    int count = read_pids(list, MAX_PIDS);
    if (count < 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (shutdown_requested) {
            log_info("Shutdown requested, stopping resource checks.");
            break;
        }
        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/status", (int)list[i]);
        FILE *f = fopen(path, "r");
        if (!f) {
            log_error("Cannot open %s: %s", path, strerror(errno));
            continue;
        }
        printf("=== PID %d ===\n", (int)list[i]);
        char buf[LINE_BUFSZ];
        while (fgets(buf, sizeof(buf), f)) {
            if (strncmp(buf, "VmRSS:", 6) == 0 ||
                strncmp(buf, "VmSize:", 7) == 0 ||
                strncmp(buf, "Cpu", 3) == 0 || /* some kernels use different fields; safe filter */
                strncmp(buf, "State:", 6) == 0 ||
                strncmp(buf, "Threads:", 8) == 0) {
                printf("%s", buf);
            }
        }
        fclose(f);
    }
    return 0;
}

/* Remove PIDs that are dead from file (cleanup utility). Not required but useful. */
static int cleanup_dead_pids(void) {
    pid_t list[MAX_PIDS];
    int count = read_pids(list, MAX_PIDS);
    if (count < 0) return -1;
    pid_t keep[MAX_PIDS];
    size_t keep_count = 0;
    for (int i = 0; i < count; ++i) {
        if (validate_pid(list[i])) {
            keep[keep_count++] = list[i];
        } else {
            log_info("Removing dead PID %d from list.", (int)list[i]);
        }
    }
    if (write_pids_atomic(keep, keep_count) != 0) {
        log_error("Failed to write cleaned PID list.");
        return -1;
    }
    log_info("Cleanup done. %zu PIDs remain.", keep_count);
    return 0;
}

/* Print usage */
static void print_usage(FILE *o, const char *prog) {
    fprintf(o,
            "Usage: %s [options]\n"
            "Options:\n"
            "  -a <pid>    Add PID to process group\n"
            "  -l          List PIDs in group\n"
            "  -k          Kill all PIDs (SIGKILL)\n"
            "  -s <sig>    Send numeric signal to all (e.g., 9, 15)\n"
            "  -r          Show basic resource usage for each PID\n"
            "  -c          Cleanup dead PIDs from list\n"
            "  -h          Show this help\n",
            prog);
}

/* Main */
int main(int argc, char *argv[]) {
    /* Setup signal handlers */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        log_error("sigaction(SIGINT) failed: %s", strerror(errno));
        return 1;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        log_error("sigaction(SIGTERM) failed: %s", strerror(errno));
        return 1;
    }

    if (ensure_pidfile_exists() < 0) return 1;

    int opt;
    bool do_add = false, do_list = false, do_kill = false, do_show = false, do_cleanup = false;
    pid_t pid_to_add = -1;
    int signal_num = -1;

    while ((opt = getopt(argc, argv, "a:lkrs:ch")) != -1) {
        switch (opt) {
            case 'a': {
                char *endptr = NULL;
                errno = 0;
                long v = strtol(optarg, &endptr, 10);
                if (errno || endptr == optarg || v <= 0) {
                    log_error("Invalid PID value for -a: '%s'", optarg);
                    return 1;
                }
                pid_to_add = (pid_t)v;
                do_add = true;
                break;
            }
            case 'l':
                do_list = true;
                break;
            case 'k':
                do_kill = true;
                signal_num = SIGKILL;
                break;
            case 's': {
                char *endptr = NULL;
                errno = 0;
                long v = strtol(optarg, &endptr, 10);
                if (errno || endptr == optarg) {
                    log_error("Invalid signal number for -s: '%s'", optarg);
                    return 1;
                }
                signal_num = (int)v;
                break;
            }
            case 'r':
                do_show = true;
                break;
            case 'c':
                do_cleanup = true;
                break;
            case 'h':
            default:
                print_usage(stdout, argv[0]);
                return 0;
        }
    }

    /* Execute requested actions. Order: add -> list -> show -> signal/kill -> cleanup */
    if (do_add) {
        if (add_pid(pid_to_add) != 0) {
            log_error("Add pid failed.");
        }
    }
    if (do_list) {
        if (list_pids_cmd() != 0) {
            log_error("List failed.");
        }
    }
    if (do_show) {
        if (show_resources_cmd() != 0) {
            log_error("Show resources failed.");
        }
    }
    if (signal_num != -1) {
        if (send_signal_all(signal_num, false) != 0) {
            log_error("One or more signal deliveries failed.");
        }
    }
    if (do_cleanup) {
        if (cleanup_dead_pids() != 0) {
            log_error("Cleanup failed.");
        }
    }

    if (shutdown_requested) {
        log_info("Process interrupted by signal; exiting gracefully.");
    }
    return 0;
}
