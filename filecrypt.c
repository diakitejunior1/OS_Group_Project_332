// filecrypt.c
// Custom Encryption/Decryption Tool for Linux Shell Project
// Uses OpenSSL EVP API + mmap() + secure key entry

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define IV_SIZE 16   // AES block size

void secure_get_key(char *buffer, size_t size) {
    struct termios oldt, newt;
    printf("Enter key: ");
    fflush(stdout);

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    fgets(buffer, size, stdin);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");

    buffer[strcspn(buffer, "\n")] = 0; // strip newline
}

int encrypt_file(const char *input, const char *output, const EVP_CIPHER *cipher, unsigned char *key) {
    int fd = open(input, O_RDONLY);
    if (fd < 0) {
        perror("Error opening input file");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return 1;
    }

    size_t size = st.st_size;
    unsigned char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    int out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) {
        perror("Error opening output file");
        return 1;
    }

    unsigned char iv[IV_SIZE];
    RAND_bytes(iv, IV_SIZE);
    write(out_fd, iv, IV_SIZE); // store IV at beginning

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len, ciphertext_len;
    unsigned char *ciphertext = malloc(size + EVP_MAX_BLOCK_LENGTH);

    EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &len, map, size);
    ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    ciphertext_len += len;

    write(out_fd, ciphertext, ciphertext_len);

    EVP_CIPHER_CTX_free(ctx);
    munmap(map, size);
    close(fd);
    close(out_fd);
    free(ciphertext);

    return 0;
}

int decrypt_file(const char *input, const char *output, const EVP_CIPHER *cipher, unsigned char *key) {
    int fd = open(input, O_RDONLY);
    if (fd < 0) {
        perror("Error opening input file");
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    size_t size = st.st_size;

    if (size < IV_SIZE) {
        fprintf(stderr, "Invalid encrypted file\n");
        return 1;
    }

    unsigned char iv[IV_SIZE];
    read(fd, iv, IV_SIZE);

    // macOS fix: mmap must be page aligned, so mmap from 0
    unsigned char *map_full = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_full == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    unsigned char *map = map_full + IV_SIZE; // skip IV manually
    size_t ciphertext_size = size - IV_SIZE;

    int out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) {
        perror("Error opening output file");
        return 1;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len, plaintext_len;
    unsigned char *plaintext = malloc(ciphertext_size + EVP_MAX_BLOCK_LENGTH);

    EVP_DecryptInit_ex(ctx, cipher, NULL, key, iv);
    EVP_DecryptUpdate(ctx, plaintext, &len, map, ciphertext_size);
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) == 0) {
        fprintf(stderr, "Decryption failed (bad key?)\n");
        return 1;
    }
    plaintext_len += len;

    write(out_fd, plaintext, plaintext_len);

    EVP_CIPHER_CTX_free(ctx);
    munmap(map_full, size);
    close(fd);
    close(out_fd);
    free(plaintext);

    return 0;
}

void print_help() {
    printf("Usage: filecrypt [OPTIONS]\n");
    printf("  -e, --encrypt       Encrypt file\n");
    printf("  -d, --decrypt       Decrypt file\n");
    printf("  -i, --input <file>  Input file\n");
    printf("  -o, --output <file> Output file\n");
    printf("  -a, --algorithm     aes-128 or aes-256\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char **argv) {
    int mode = 0; 
    char *input = NULL, *output = NULL, *algo = "aes-256";
    unsigned char key[32] = {0};

    struct option long_opts[] = {
        {"encrypt", no_argument, 0, 'e'},
        {"decrypt", no_argument, 0, 'd'},
        {"input",   required_argument, 0, 'i'},
        {"output",  required_argument, 0, 'o'},
        {"algorithm", required_argument, 0, 'a'},
        {"help",    no_argument, 0, 'h'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "edi:o:a:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'e': mode = 1; break;
            case 'd': mode = 2; break;
            case 'i': input = optarg; break;
            case 'o': output = optarg; break;
            case 'a': algo = optarg; break;
            case 'h': print_help(); return 0;
            default: print_help(); return 1;
        }
    }

    if (!mode || !input || !output) {
        print_help();
        return 1;
    }

    const EVP_CIPHER *cipher;

    if (strcmp(algo, "aes-128") == 0) {
        cipher = EVP_aes_128_cbc();
    } else if (strcmp(algo, "aes-256") == 0) {
        cipher = EVP_aes_256_cbc();
    } else {
        fprintf(stderr, "Invalid algorithm.\n");
        return 1;
    }

    printf("Enter key (%s):\n", algo);
    secure_get_key((char *)key, sizeof(key));

    if (mode == 1)
        return encrypt_file(input, output, cipher, key);
    else
        return decrypt_file(input, output, cipher, key);
}
