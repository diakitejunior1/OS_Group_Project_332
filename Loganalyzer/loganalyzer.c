#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>

volatile sig_atomic_t stopFlag = 0;

void handle_sigint(int signo) {
    stopFlag = 1;
    printf("\n[!] Program interrupted. Cleaning up...\n");
}

// ====== THREAD STRUCT ======
typedef struct {
    char *data;
    size_t start;
    size_t end;
    const char *keyword;
    long count;
} ThreadArg;

// ====== THREAD WORKER ======
void *search_worker(void *arg) {
    ThreadArg *t = (ThreadArg*)arg;

    for (size_t i = t->start; i < t->end; i++) {
        if (stopFlag) break;
        if (strncmp(&t->data[i], t->keyword, strlen(t->keyword)) == 0) {
            t->count++;
        }
    }
    return NULL;
}

// ====== HELP MENU ======
void print_help() {
    printf("Usage: loganalyzer [OPTIONS]\n\n"
           "Options:\n"
           "  -h, --help               Show help menu\n"
           "  -f, --file <path>        Path to log file (required)\n"
           "  -k, --keyword <word>     Count occurrences of keyword\n"
           "  -e, --error              Count error-level lines\n"
           "  -s, --stats              Show file statistics\n"
           "  -t, --threads <N>        Enable multithreaded search\n"
           "  -m, --memory             Show memory map statistics\n");
}

// ====== MAIN ======
int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);

    char *filepath = NULL;
    char *keyword = NULL;
    int show_error = 0;
    int show_stats = 0;
    int show_memory = 0;
    int thread_count = 1;
    long line_limit = -1;

    struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"file",    required_argument, 0, 'f'},
        {"keyword", required_argument, 0, 'k'},
        {"error",   no_argument,       0, 'e'},
        {"stats",   no_argument,       0, 's'},
        {"threads", required_argument, 0, 't'},
        {"memory",  no_argument,       0, 'm'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "hf:k:est:ml:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h': print_help(); return 0;
            case 'f': filepath = optarg; break;
            case 'k': keyword = optarg; break;
            case 'e': show_error = 1; break;
            case 's': show_stats = 1; break;
            case 'm': show_memory = 1; break;
            case 't': thread_count = atoi(optarg); break;
            default: print_help(); return 1;
        }
    }

    if (!filepath) {
        fprintf(stderr, "Error: --file is required.\n");
        return 1;
    }

    // ====== OPEN FILE ======
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    size_t filesize = st.st_size;

    // ====== MMAP FILE ======
    char *map = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    if (show_memory) {
        printf("Mapped file size: %zu bytes\n", filesize);
        printf("Start address: %p\n\n", map);
    }

    long keyword_count = 0;
    long error_count = 0;
    long line_count = 0;

    // ====== LINE COUNT + ERROR DETECTION ======
    for (size_t i = 0; i < filesize; i++) {
        if (map[i] == '\n') line_count++;

        if (stopFlag) break;

        if (show_error) {
            if (strncmp(&map[i], "ERROR", 5) == 0 ||
                strncmp(&map[i], "WARN", 4) == 0 ||
                strncmp(&map[i], "CRIT", 4) == 0) {
                error_count++;
            }
        }
    }

    // ====== MULTITHREADED KEYWORD SEARCH ======
    if (keyword) {
        pthread_t threads[thread_count];
        ThreadArg args[thread_count];
        size_t chunk = filesize / thread_count;

        for (int i = 0; i < thread_count; i++) {
            args[i].data = map;
            args[i].start = i * chunk;
            args[i].end = (i == thread_count - 1) ? filesize : (i+1)*chunk;
            args[i].keyword = keyword;
            args[i].count = 0;
            pthread_create(&threads[i], NULL, search_worker, &args[i]);
        }

        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
            keyword_count += args[i].count;
        }
    }

    // ====== OUTPUT ======
    if (show_stats)
        printf("[STATS] Total lines: %ld\n", line_count);

    if (show_error)
        printf("[ERROR] Error-like lines: %ld\n", error_count);

    if (keyword)
        printf("[KEYWORD] '%s' found %ld times\n", keyword, keyword_count);

    munmap(map, filesize);
    close(fd);

    return 0;
}

