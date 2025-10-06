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

    //Compilar regex 
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED | REG_ICASE) != 0) {
        fprintf(stderr, "Error al compilar regex\n");
        exit(EXIT_FAILURE);
    }

    //Tamaño del archivo
    FILE *file = fopen(filename, "r");
    if (!file) { perror("fopen"); exit(EXIT_FAILURE); }
    fseek(file, 0, SEEK_END);
    off_t file_size = ftell(file);
    fclose(file);

    //Se crean lo logs
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

    //Pipes 
    int request_pipe[2], assign_pipe[2];
    pipe(request_pipe);
    pipe(assign_pipe);

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
            close(request_pipe[0]);
            close(assign_pipe[1]);

            FILE *fp = fopen(filename, "r");
            if (!fp) exit(EXIT_FAILURE);

            while (1) {
                struct message msg;
                msg.type = REQUEST_WORK;
                msg.pid = getpid();
                write(request_pipe[1], &msg, sizeof(msg));

                if (read(assign_pipe[0], &msg, sizeof(msg)) != sizeof(msg))
                    break;

                if (msg.type == FINISH_WORK) {
                    printf("[HIJO %d] Finalizando\n", getpid());
                    break;
                }

                off_t start = msg.start_pos;
                off_t end = msg.end_pos;
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

                struct message res;
                res.type = REPORT_RESULT;
                res.pid = getpid();
                res.start_pos = start;
                res.end_pos = end;
                res.lines_found = lines;
                res.bytes_processed = n;
                res.start_time = t1;
                res.end_time = t2;
                res.process_time = time_diff(t1, t2);

                write(request_pipe[1], &res, sizeof(res));
            }

            fclose(fp);
            close(request_pipe[1]);
            close(assign_pipe[0]);
            regfree(&regex);
            exit(EXIT_SUCCESS);
        } else {
            children[i] = pid;
        }
    }

    //-------------------------------------
    //            PADRE
    //-------------------------------------
    close(request_pipe[1]);
    close(assign_pipe[0]);

    off_t next_pos = 0;
    int finished = 0;

    FILE *log = fopen("grep_log.csv", "a");

    while (finished < num_proc) {
        struct message msg;
        ssize_t r = read(request_pipe[0], &msg, sizeof(msg));
        if (r != sizeof(msg)) continue;

        if (msg.type == REQUEST_WORK) {
            if (next_pos >= file_size) {
                struct message fin = { .type = FINISH_WORK, .pid = msg.pid };
                write(assign_pipe[1], &fin, sizeof(fin));
                finished++;
            } else {
                struct message a;
                a.type = ASSIGN_BLOCK;
                a.pid = msg.pid;
                a.start_pos = next_pos;
                a.end_pos = next_pos + BUFFER_SIZE;
                if (a.end_pos > file_size) a.end_pos = file_size;
                write(assign_pipe[1], &a, sizeof(a));
                next_pos = a.end_pos;
                printf("[PADRE] Asignando bloque %ld-%ld a %d\n", (long)a.start_pos, (long)a.end_pos, (int)a.pid);
            }
        } else if (msg.type == REPORT_RESULT) {
            write_log_entry(log, &msg);
            stats.total_time += msg.process_time;
            stats.bytes_processed += msg.bytes_processed;
            stats.total_matches += msg.lines_found;
            if (msg.process_time > stats.max_time) stats.max_time = msg.process_time;
            if (msg.process_time < stats.min_time) stats.min_time = msg.process_time;
        }
    }

    fclose(log);
    close(request_pipe[0]);
    close(assign_pipe[1]);

    for (int i = 0; i < num_proc; i++)
        waitpid(children[i], NULL, 0);

    gettimeofday(&stats.end_time, NULL);
    analizar_rendimiento(&stats, num_proc, file_size);

    regfree(&regex);
    return 0;
}
