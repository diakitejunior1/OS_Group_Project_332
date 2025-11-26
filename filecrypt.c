
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define MAX_THREADS 32

// ---------------------------- GLOBALS ----------------------------

// Signal flag
volatile sig_atomic_t stop_requested = 0;

// Thread job structure
typedef struct {
    unsigned char *data;
    size_t start;
    size_t end;
    int key;
    int encrypt; // 1 = encrypt, 0 = decrypt
} thread_job_t;

// ---------------------------- SIGNAL HANDLING ----------------------------
void signal_handler(int sig) {
    stop_requested = 1;
    fprintf(stderr, "\n[!] Signal received (%d). Stopping safely...\n", sig);
}

// ---------------------------- SECURE KEY ENTRY ----------------------------
int secure_get_key() {
    struct termios oldt, newt;
    char buf[32];

    printf("Enter key (number): ");
    fflush(stdout);

    tcgetattr(STDIN_FILENO, &oldt);   // backup
    newt = oldt;
    newt.c_lflag &= ~ECHO;            // disable echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    fgets(buf, sizeof(buf), stdin);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // restore
    printf("\n");

    return atoi(buf);
}

// ---------------------------- CAESAR CIPHER (Thread Worker) ----------------------------
void *thread_caesar(void *arg) {
    thread_job_t *job = (thread_job_t *)arg;
    int key = job->encrypt ? job->key : -job->key;

    for (size_t i = job->start; i < job->end; i++) {
        if (stop_requested) pthread_exit(NULL);
        job->data[i] = (job->data[i] + key) % 256;
    }
    return NULL;
}

// ---------------------------- HELP MENU ----------------------------
void print_help() {
    printf("Usage: filecrypt [OPTIONS]\n");
    printf("  -e, --encrypt           Encrypt file\n");
    printf("  -d, --decrypt           Decrypt file\n");
    printf("  -i, --input <file>      Input file\n");
    printf("  -o, --output <file>     Output file\n");
    printf("  -t, --threads <num>     Number of threads (default: 4)\n");
    printf("  -h, --help              Show this help\n");
}

// ---------------------------- MAIN ----------------------------
int main(int argc, char **argv) {
    int mode = 0;          // 1=encrypt, 2=decrypt
    char *input = NULL;
    char *output = NULL;
    int thread_count = 4;

    // Install signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct option long_opts[] = {
        {"encrypt", no_argument, 0, 'e'},
        {"decrypt", no_argument, 0, 'd'},
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"threads", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "edi:o:t:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'e': mode = 1; break;
            case 'd': mode = 2; break;
            case 'i': input = optarg; break;
            case 'o': output = optarg; break;
            case 't': thread_count = atoi(optarg); break;
            case 'h': print_help(); return 0;
            default: print_help(); return 1;
        }
    }

    if (!mode || !input || !output) {
        print_help();
        return 1;
    }

    if (thread_count < 1 || thread_count > MAX_THREADS) {
        fprintf(stderr, "Invalid thread count. Must be 1-%d.\n", MAX_THREADS);
        return 1;
    }

    // Ask for key
    int key = secure_get_key();

    // Open file
    int fd = open(input, O_RDONLY);
    if (fd < 0) { perror("open input"); return 1; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return 1; }

    size_t size = st.st_size;
    if (size == 0) {
        fprintf(stderr, "Input file is empty.\n");
        close(fd);
        return 1;
    }

    // mmap the input file
    unsigned char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    // Prepare output buffer
    unsigned char *buffer = malloc(size);
    if (!buffer) { perror("malloc"); return 1; }
    memcpy(buffer, map, size);

    close(fd);

    // Prepare threads
    pthread_t threads[MAX_THREADS];
    thread_job_t jobs[MAX_THREADS];

    // Timing
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    size_t chunk = size / thread_count;

    // Launch worker threads
    for (int i = 0; i < thread_count; i++) {
        jobs[i].data = buffer;
        jobs[i].start = i * chunk;
        jobs[i].end = (i == thread_count - 1) ? size : (i + 1) * chunk;
        jobs[i].key = key;
        jobs[i].encrypt = (mode == 1);

        pthread_create(&threads[i], NULL, thread_caesar, &jobs[i]);
    }

    // Wait for threads
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Save output
    int out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) { perror("open output"); return 1; }

    if (write(out_fd, buffer, size) != size) {
        perror("write");
        close(out_fd);
        return 1;
    }
    close(out_fd);

    // Cleanup
    munmap(map, size);
    free(buffer);

    // Performance info
    double elapsed =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n[+] Operation completed successfully.\n");
    printf("[+] File size: %zu bytes\n", size);
    printf("[+] Threads used: %d\n", thread_count);
    printf("[+] Time taken: %.4f seconds\n", elapsed);

    if (stop_requested)
        printf("[!] Warning: Operation interrupted by signal.\n");

    return 0;
}
