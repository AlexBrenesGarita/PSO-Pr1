// grep2.c — 1 proceso, lectura en bloques 8 KB sin cortar líneas
// Uso: ./grep2 "regex" archivo.txt
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#define CHUNK 8192

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s 'regex' archivo\n", argv[0]);
        return 1;
    }
    const char *pattern = argv[1];
    const char *path    = argv[2];

    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }

    regex_t re;
    int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
    if (rc != 0) {
        char buf[256];
        regerror(rc, &re, buf, sizeof(buf));
        fprintf(stderr, "regcomp: %s\n", buf);
        fclose(f);
        return 1;
    }

    char *carry = NULL; size_t carry_len = 0;

    for (;;) {
        char buf[CHUNK];
        size_t r = fread(buf, 1, CHUNK, f);
        if (r == 0) break;

        // Busca el último '\n' del bloque para no cortar línea
        long last_nl = -1;
        for (long i = (long)r - 1; i >= 0; --i) {
            if (buf[i] == '\n') { last_nl = i; break; }
        }
        size_t process_len = (last_nl >= 0) ? (size_t)(last_nl + 1) : r;

        // work = carry + porción completa actual
        size_t work_len = carry_len + process_len;
        char *work = (char*)malloc(work_len + 1);
        if (!work) { perror("malloc"); return 1; }
        if (carry_len) memcpy(work, carry, carry_len);
        memcpy(work + carry_len, buf, process_len);
        work[work_len] = '\0';

        // Procesar línea por línea en "work"
        char *p = work, *e = work + work_len;
        while (p < e) {
            char *nl = memchr(p, '\n', (size_t)(e - p));
            size_t len = nl ? (size_t)(nl - p) : (size_t)(e - p);
            // Copiamos a buffer temporal para asegurar '\0'
            char *line = (char*)malloc(len + 1);
            if (!line) { perror("malloc"); return 1; }
            memcpy(line, p, len); line[len] = '\0';

            if (regexec(&re, line, 0, NULL, 0) == 0) {
                puts(line);
            }
            free(line);
            p += len + (nl ? 1 : 0);
        }

        free(work);
        free(carry); carry = NULL; carry_len = 0;

        // Si hubo corte de línea, preparar carry = resto del bloque sin procesar
        if (process_len < r) {
            size_t rest = r - process_len;
            carry = (char*)malloc(rest);
            if (!carry) { perror("malloc"); return 1; }
            memcpy(carry, buf + process_len, rest);
            carry_len = rest;

            // Retroceder el puntero de archivo para "releer" desde el inicio del resto
            if (fseek(f, -(long)rest, SEEK_CUR) != 0) {
                perror("fseek");
                return 1;
            }
        }
    }

    // Si al final quedó una línea sin '\n' en carry, procesarla
    if (carry_len) {
        carry = (char*)realloc(carry, carry_len + 1);
        carry[carry_len] = '\0';
        if (regexec(&re, carry, 0, NULL, 0) == 0) puts(carry);
        free(carry);
    }

    regfree(&re);
    fclose(f);
    return 0;
}

