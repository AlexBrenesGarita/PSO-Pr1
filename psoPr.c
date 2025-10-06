//-------------------------------------
//            INCLUDES
//-------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>

//-------------------------------------
//            DEFINES
//-------------------------------------
#define BUFFER_SIZE 8192

//-------------------------------------
//            STRUCTS
//-------------------------------------
struct stats {
    double total_time;
    double max_time;
    double min_time;
    off_t bytes_processed;
    int total_matches;
    double throughput;
    struct timeval start_time;
    struct timeval end_time;
};

struct message {
    enum { REQUEST_WORK, ASSIGN_BLOCK, REPORT_RESULT, FINISH_WORK } type;
    pid_t pid;
    off_t start_pos;
    off_t end_pos;
    double process_time;
    int lines_found;
    off_t bytes_processed;
    struct timeval start_time;
    struct timeval end_time;
};

// Estructura para cola de mensajes
struct msgbuf {
    long mtype;          // Tipo de mensaje (pid del hijo o constante)
    struct message msg;
};

//-------------------------------------
//            FUNCIONES
//-------------------------------------
double time_diff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
}

void print_separator() {
    printf("\n================================================\n");
}

void write_log_entry(FILE *log_file, struct message *msg) {
    double elapsed = time_diff(msg->start_time, msg->end_time);
    fprintf(log_file, "%d,%ld,%ld,%.6f,%.6f,%d,%ld,%ld\n",
            (int)msg->pid,
            (long)msg->start_pos,
            (long)msg->end_pos,
            msg->process_time,
            elapsed,
            msg->lines_found,
            (long)msg->bytes_processed,
            (long)(msg->end_pos - msg->start_pos));
    fflush(log_file);
}

void analizar_rendimiento(struct stats *stats, int num_processes, off_t file_size) {
    double total_elapsed = time_diff(stats->start_time, stats->end_time);
    stats->throughput = (total_elapsed > 0.0) ? ((double)stats->bytes_processed / total_elapsed) : 0.0;

    print_separator();
    printf(" ANÁLISIS DE RENDIMIENTO\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" Procesos: %d\n", num_processes);
    printf(" Tamaño archivo: %ld bytes\n", (long)file_size);
    printf(" Bytes procesados: %ld\n", (long)stats->bytes_processed);
    printf(" Coincidencias: %d\n", stats->total_matches);
    printf(" Tiempo total: %.6f s\n", total_elapsed);
    printf(" Tiempo CPU acumulado: %.6f s\n", stats->total_time);
    printf(" Min tiempo bloque: %.6f s\n", stats->min_time);
    printf(" Max tiempo bloque: %.6f s\n", stats->max_time);
    printf(" Velocidad promedio: %.2f MB/s\n", (stats->throughput / 1048576.0));
    print_separator();

    FILE *summary = fopen("performance_summary.csv", "a");
    if (summary) {
        fprintf(summary, "%d,%.6f,%.6f,%.6f,%.6f,%.2f\n",
                num_processes,
                total_elapsed,
                stats->total_time,
                stats->min_time,
                stats->max_time,
                stats->throughput / 1048576.0);
        fclose(summary);
    }
}

//-------------------------------------
//            MAIN
//-------------------------------------
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s \"regex\" archivo.txt num_procesos\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *pattern = argv[1];
    char *filename = argv[2];
    int num_proc = atoi(argv[3]);
    if (num_proc <= 0) {
        fprintf(stderr, "num_procesos debe ser > 0\n");
        exit(EXIT_FAILURE);
    }

    // Compilar regex
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED | REG_ICASE) != 0) {
        fprintf(stderr, "Error al compilar regex\n");
        exit(EXIT_FAILURE);
    }

    // Tamaño del archivo
    FILE *file = fopen(filename, "r");
    if (!file) { perror("fopen"); exit(EXIT_FAILURE); }
    fseek(file, 0, SEEK_END);
    off_t file_size = ftell(file);
    fclose(file);

    // Crear logs
    FILE *log_file = fopen("grep_log.csv", "w");
    fprintf(log_file, "pid,start_pos,end_pos,process_time,total_time,lines_found,bytes_processed,block_size\n");
    fclose(log_file);

    FILE *summary = fopen("performance_summary.csv", "a");
    if (summary) {
        fseek(summary, 0, SEEK_END);
        long pos = ftell(summary);
        if (pos == 0)
            fprintf(summary, "num_processes,total_time,cpu_time,min_block_time,max_block_time,throughput_MB_s\n");
        fclose(summary);
    }

    // Crear cola de mensajes
    key_t key = ftok("grep_log.csv", 'B');
    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid == -1) { perror("msgget"); exit(EXIT_FAILURE); }

    struct stats stats = {0};
    gettimeofday(&stats.start_time, NULL);
    stats.min_time = 1e9;

    pid_t children[num_proc];
    for (int i = 0; i < num_proc; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            //-------------------------------------
            //            HIJOS
            //-------------------------------------
            FILE *fp = fopen(filename, "r");
            if (!fp) exit(EXIT_FAILURE);

            while (1) {
                // Solicitar trabajo
                struct msgbuf request;
                request.mtype = 1;
                request.msg.type = REQUEST_WORK;
                request.msg.pid = getpid();
                msgsnd(msqid, &request, sizeof(request.msg), 0);

                // Recibir asignación
                struct msgbuf assign;
                msgrcv(msqid, &assign, sizeof(assign.msg), getpid(), 0);

                if (assign.msg.type == FINISH_WORK) {
                    printf("[HIJO %d] Finalizando\n", getpid());
                    break;
                }

                off_t start = assign.msg.start_pos;
                off_t end = assign.msg.end_pos;
                size_t toread = end - start;

                char buffer[BUFFER_SIZE + 1];
                fseek(fp, start, SEEK_SET);
                size_t n = fread(buffer, 1, toread, fp);
                buffer[n] = '\0';

                struct timeval t1, t2;
                gettimeofday(&t1, NULL);

                int lines = 0;
                char *line = strtok(buffer, "\n");
                while (line) {
                    if (regexec(&regex, line, 0, NULL, 0) == 0) {
                        printf("[HIJO %d] Coincidencia: %s\n", getpid(), line);
                        lines++;
                    }
                    line = strtok(NULL, "\n");
                }

                gettimeofday(&t2, NULL);

                // Reportar resultado
                struct msgbuf res;
                res.mtype = 1;
                res.msg.type = REPORT_RESULT;
                res.msg.pid = getpid();
                res.msg.start_pos = start;
                res.msg.end_pos = end;
                res.msg.lines_found = lines;
                res.msg.bytes_processed = n;
                res.msg.start_time = t1;
                res.msg.end_time = t2;
                res.msg.process_time = time_diff(t1, t2);

                msgsnd(msqid, &res, sizeof(res.msg), 0);
            }

            fclose(fp);
            regfree(&regex);
            exit(EXIT_SUCCESS);
        } else {
            children[i] = pid;
        }
    }

    //-------------------------------------
    //            PADRE
    //-------------------------------------
    off_t next_pos = 0;
    int finished = 0;

    FILE *log = fopen("grep_log.csv", "a");

    while (finished < num_proc) {
        struct msgbuf msg;
        ssize_t r = msgrcv(msqid, &msg, sizeof(msg.msg), 1, 0);
        if (r != sizeof(msg.msg)) continue;

        if (msg.msg.type == REQUEST_WORK) {
            if (next_pos >= file_size) {
                struct msgbuf fin;
                fin.mtype = msg.msg.pid;
                fin.msg.type = FINISH_WORK;
                fin.msg.pid = msg.msg.pid;
                msgsnd(msqid, &fin, sizeof(fin.msg), 0);
                finished++;
            } else {
                struct msgbuf a;
                a.mtype = msg.msg.pid;
                a.msg.type = ASSIGN_BLOCK;
                a.msg.pid = msg.msg.pid;
                a.msg.start_pos = next_pos;
                a.msg.end_pos = next_pos + BUFFER_SIZE;
                if (a.msg.end_pos > file_size) a.msg.end_pos = file_size;
                msgsnd(msqid, &a, sizeof(a.msg), 0);
                printf("[PADRE] Asignando bloque %ld-%ld a %d\n", (long)a.msg.start_pos, (long)a.msg.end_pos, (int)a.msg.pid);
                next_pos = a.msg.end_pos;
            }
        } else if (msg.msg.type == REPORT_RESULT) {
            write_log_entry(log, &msg.msg);
            stats.total_time += msg.msg.process_time;
            stats.bytes_processed += msg.msg.bytes_processed;
            stats.total_matches += msg.msg.lines_found;
            if (msg.msg.process_time > stats.max_time) stats.max_time = msg.msg.process_time;
            if (msg.msg.process_time < stats.min_time) stats.min_time = msg.msg.process_time;
        }
    }

    fclose(log);

    for (int i = 0; i < num_proc; i++)
        waitpid(children[i], NULL, 0);

    gettimeofday(&stats.end_time, NULL);
    analizar_rendimiento(&stats, num_proc, file_size);

    // Eliminar cola de mensajes
    msgctl(msqid, IPC_RMID, NULL);
    regfree(&regex);
    return 0;
}
