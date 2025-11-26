#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#define BUF 1024


// TEXT DIFF: 
   
void text_diff(int fd1, int fd2) {
    char buf1[BUF], buf2[BUF];
    int line = 1;

    printf(" - TEXT DIFF -\n");

    // Convert fd to FILE* for fgets 
    FILE *f1 = fdopen(fd1, "r");
    FILE *f2 = fdopen(fd2, "r");

    if (!f1 || !f2) {    
        fprintf(stderr, "fdopen error: cannot convert file descriptor.\n");  //Error Handling for fopen
        return;
    }

    while (1) {
        char *l1 = fgets(buf1, BUF, f1);
        char *l2 = fgets(buf2, BUF, f2);

        if (!l1 && !l2) break;  // both ended

        // If one line is NULL or content mismatch
        if (!l1 || !l2 || strcmp(buf1, buf2) != 0) {
            printf("Line %d different\n", line);
        }

        line++;
    }

    // Reset file offset for binary diff
    lseek(fd1, 0, SEEK_SET);
    lseek(fd2, 0, SEEK_SET);
}

// BINARY DIFF: 
   
long binary_diff(int fd1, int fd2, long size1, long size2) {
    unsigned char b1, b2;
    long diff = 0;

    printf("\n- BINARY DIFF -\n");

    long min = (size1 < size2) ? size1 : size2;

    for (long i = 0; i < min; i++) {
        if (read(fd1, &b1, 1) != 1 || read(fd2, &b2, 1) != 1) {   //Error Handling for read
            fprintf(stderr, "Read error during binary diff.\n")
            break;
        }
        if (b1 != b2) diff++;
    }

    // Add remaining size difference
    diff += labs(size1 - size2);

    printf("Different bytes: %ld\n", diff);
    return diff;
}

// MAIN 
    
int main(int argc, char *argv[]) {


    // Check argument count
    if (argc < 4) {
        printf("Usage: filediffadvanced <mode> file1 file2\n");
        printf("Modes: text | binary\n");
        return 1;
    }

    char *mode = argv[1];
    char *file1 = argv[2];
    char *file2 = argv[3];

    // Open files 
    int fd1 = open(file1, O_RDONLY);
    int fd2 = open(file2, O_RDONLY);

    if (fd1 < 0 || fd2 < 0) {
        perror("open"); //Error Handling for open
        return 1;
    }

    // Retrieve file size
    struct stat s1, s2;
    if (stat(file1, &s1) < 0 || stat(file2, &s2) < 0) {
        perror("stat"); //Error Handling for stat
        return 1;
    }

    // Performance timer start
    struct timeval start, end;
    gettimeofday(&start, NULL);

    // TEXT DIFF
    if (strcmp(mode, "text") == 0) {
        text_diff(fd1, fd2);
    }

    // BINARY DIFF
    long diff = 0;
    if (strcmp(mode, "binary") == 0) {
        diff = binary_diff(fd1, fd2, s1.st_size, s2.st_size);
    }

    // Performance timer end
    gettimeofday(&end, NULL);
    long ms = (end.tv_sec - start.tv_sec) * 1000 +
              (end.tv_usec - start.tv_usec) / 1000;

    // Final statistics
    printf("\n - STATS -\n");
    printf("File1: %ld bytes\n", s1.st_size);
    printf("File2: %ld bytes\n", s2.st_size);
    printf("Time: %ld ms\n", ms);

    // Close file descriptors
    close(fd1);
    close(fd2);

    return 0;
}