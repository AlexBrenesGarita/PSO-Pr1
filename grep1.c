// grep1.c � grep simple de 1 proceso (lee l�nea por l�nea)
// Uso: ./grep1 "patron|otro" archivo.txt

#include <stdio.h>    // fopen, getline, printf, perror, etc.
#include <stdlib.h>   // exit, malloc, free
#include <string.h>   // funciones de cadena
#include <regex.h>    // regcomp, regexec, regfree

int main(int argc, char **argv) {
    // 1) Validar argumentos: patr�n y archivo
    if (argc != 3) {
        fprintf(stderr, "Uso: %s 'regex' archivo\n", argv[0]);
        return 1;
    }
    const char *pattern = argv[1];  // expresi�n regular
    const char *path    = argv[2];  // ruta del archivo

    // 2) Abrir archivo en modo texto lectura
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return 1; }

    // 3) Compilar la expresi�n regular una sola vez
    regex_t re;
    int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
    if (rc != 0) {
        char buf[256];
        regerror(rc, &re, buf, sizeof(buf));
        fprintf(stderr, "Error en regcomp: %s\n", buf);
        fclose(f);
        return 1;
    }

    // 4) Leer l�nea por l�nea (POSIX getline)
    char   *line = NULL;   // buffer que getline asigna/crece
    size_t  cap  = 0;      // capacidad del buffer
    ssize_t n;             // longitud le�da

    while ((n = getline(&line, &cap, f)) != -1) {
        // 5) Evaluar la regex sobre la l�nea
        // regexec retorna 0 si HAY coincidencia
        if (regexec(&re, line, 0, NULL, 0) == 0) {
            fputs(line, stdout); // 6) Imprimir la l�nea que matchea
        }
    }

    // 7) Liberar recursos
    free(line);
    regfree(&re);
    fclose(f);
    return 0;
}

