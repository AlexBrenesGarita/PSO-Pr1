#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#include <sys/time.h>  // Para gettimeofday

#define BUFFER_SIZE 8192  // Buffer de 8K por proceso

// Estructura para estadísticas de rendimiento
struct stats {
    double total_time;
    double max_time;
    double min_time;
    off_t bytes_processed;
    int total_matches;
    double throughput;
    struct timeval start_time;  // Tiempo inicio global
    struct timeval end_time;    // Tiempo fin global
};

// Estructura para control de alternancia
struct turn_control {
    pid_t last_pid;
    int current_turn;
    int *process_order;
};

// Estructura para mensajes entre procesos
struct message {
    enum {
        REQUEST_WORK,
        ASSIGN_BLOCK,
        REPORT_RESULT,
        FINISH_WORK,
        WAIT_TURN
    } type;
    pid_t pid;
    off_t start_pos;
    off_t end_pos;
    double process_time;
    double block_time;
    int lines_found;
    off_t bytes_processed;
    struct timeval start_time;  // Tiempo inicio del bloque
    struct timeval end_time;    // Tiempo fin del bloque
};

void print_separator() {
    printf("\n================================================\n");
}

// Función para calcular diferencia de tiempo en segundos
double time_diff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + 
           (end.tv_usec - start.tv_usec) / 1000000.0;
}

void write_log_entry(FILE *log_file, struct message *msg) {
    double elapsed = time_diff(msg->start_time, msg->end_time);
    
    fprintf(log_file, "%d,%ld,%ld,%.6f,%.6f,%d,%ld,%ld\n",
        msg->pid,
        msg->start_pos,
        msg->end_pos,
        msg->process_time,    // Tiempo de procesamiento
        elapsed,              // Tiempo total incluyendo I/O
        msg->lines_found,
        msg->bytes_processed,
        msg->end_pos - msg->start_pos  // Tamaño del bloque
    );
}

// Función para encontrar el final de la última línea completa
off_t find_last_complete_line(char *buffer, size_t size) {
    off_t pos = size - 1;
    while (pos >= 0 && buffer[pos] != '\n') {
        pos--;
    }
    return (pos >= 0) ? pos + 1 : size;
}

// Función para inicializar control de turnos
struct turn_control* init_turn_control(int num_processes) {
    struct turn_control* tc = malloc(sizeof(struct turn_control));
    tc->last_pid = 0;
    tc->current_turn = 0;
    tc->process_order = malloc(num_processes * sizeof(int));
    for (int i = 0; i < num_processes; i++) {
        tc->process_order[i] = i;
    }
    return tc;
}

// Función para verificar turno del proceso
int is_process_turn(struct turn_control* tc, pid_t pid, int num_processes) {
    if (tc->last_pid == pid) {
        return 0;
    }
    if (tc->process_order[tc->current_turn] == pid % num_processes) {
        tc->current_turn = (tc->current_turn + 1) % num_processes;
        tc->last_pid = pid;
        return 1;
    }
    return 0;
}

// Función para análisis de rendimiento
void analizar_rendimiento(struct stats *stats, int num_processes, off_t file_size) {
    double total_elapsed = time_diff(stats->start_time, stats->end_time);
    stats->throughput = stats->bytes_processed / total_elapsed;
    
    print_separator();
    printf(" ANÁLISIS DETALLADO DE RENDIMIENTO\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" Estadísticas generales:\n");
    printf("• Procesos utilizados: %d\n", num_processes);
    printf("• Tamaño del archivo: %ld bytes\n", file_size);
    printf("• Bytes procesados: %ld\n", stats->bytes_processed);
    printf("• Coincidencias encontradas: %d\n", stats->total_matches);
    printf("\n Tiempos de ejecución:\n");
    printf("• Tiempo total real: %.6f segundos\n", total_elapsed);
    printf("• Tiempo CPU total: %.6f segundos\n", stats->total_time);
    printf("• Tiempo mínimo por bloque: %.6f segundos\n", stats->min_time);
    printf("• Tiempo máximo por bloque: %.6f segundos\n", stats->max_time);
    printf("\n Métricas de rendimiento:\n");
    printf("• Velocidad promedio: %.2f MB/s\n", (stats->throughput / 1048576.0));
    printf("• Eficiencia de paralelización: %.2f%%\n", 
           (stats->min_time / stats->max_time) * 100);
    print_separator();
    
    // Escribir resumen en archivo separado
    FILE *summary = fopen("performance_summary.csv", "a");
    if (summary) {
        fprintf(summary, "%d,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f\n",
            num_processes,
            total_elapsed,
            stats->total_time,
            stats->min_time,
            stats->max_time,
            stats->throughput / 1048576.0,
            (stats->min_time / stats->max_time) * 100);
        fclose(summary);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_separator();
        fprintf(stderr, "ERROR: Formato incorrecto\n");
        fprintf(stderr, "Uso: %s <patron> <archivo> <num_procesos>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s \"amor|guerra\" largas.txt 4\n", argv[0]);
        print_separator();
        exit(1);
    }

    char *pattern = argv[1];
    char *filename = argv[2];
    int num_processes = atoi(argv[3]);

    // Compilar la expresión regular
    regex_t regex;
    int reti = regcomp(&regex, pattern, REG_EXTENDED);
    if (reti) {
        print_separator();
        fprintf(stderr, "ERROR: Expresión regular inválida\n");
        print_separator();
        exit(1);
    }

    print_separator();
    printf(" INICIANDO BÚSQUEDA MULTIPROCESO\n");
    printf(" Patrón a buscar: '%s'\n", pattern);
    printf(" Archivo: %s\n", filename);
    printf(" Número de procesos: %d\n", num_processes);
    print_separator();

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf(" ERROR: No se pudo abrir el archivo %s\n", filename);
        perror("Detalle del error");
        print_separator();
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    off_t file_size = ftell(file);
    fclose(file);

    // Inicializar estructuras de control y estadísticas
    struct stats performance_stats = {0};
    gettimeofday(&performance_stats.start_time, NULL);
    performance_stats.min_time = 999999.0;

    struct turn_control* tc = init_turn_control(num_processes);

    // Crear pipes para comunicación
    int request_pipe[2];
    int assign_pipe[2];
    if (pipe(request_pipe) == -1 || pipe(assign_pipe) == -1) {
        printf(" ERROR: Fallo en la creación de pipes\n");
        print_separator();
        exit(1);
    }

    printf("[PADRE] Creando archivo de registro grep_log.csv\n");
    FILE *log_file = fopen("grep_log.csv", "w");
    fprintf(log_file, "pid,start_pos,end_pos,process_time,total_time,lines_found,bytes_processed,block_size\n");

    // Crear archivo de resumen si no existe
    FILE *summary = fopen("performance_summary.csv", "w");
    if (summary) {
        fprintf(summary, "num_processes,total_time,cpu_time,min_block_time,max_block_time,throughput_MB_s,efficiency_percent\n");
        fclose(summary);
    }

    printf("[PADRE] Creando pool de %d procesos...\n", num_processes);
    print_separator();

    // Crear procesos hijos
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {  // Proceso hijo
            close(request_pipe[0]);
            close(assign_pipe[1]);

            printf("[HIJO %d]  Proceso iniciado - Buffer asignado: %d bytes\n", 
                   getpid(), BUFFER_SIZE);

            char buffer[BUFFER_SIZE + 1];  // +1 para el carácter nulo
            while (1) {
                struct message msg = {
                    .type = REQUEST_WORK,
                    .pid = getpid()
                };
                
                write(request_pipe[1], &msg, sizeof(msg));
                printf("[HIJO %d] Solicitando trabajo\n", getpid());

                read(assign_pipe[0], &msg, sizeof(msg));

                if (msg.type == FINISH_WORK) {
                    printf("[HIJO %d] Finalizando proceso\n", getpid());
                    print_separator();
                    break;
                }

                if (msg.type == WAIT_TURN) {
                    printf("[HIJO %d] Esperando turno...\n", getpid());
                    continue;
                }

                printf("[HIJO %d] Procesando bloque %ld-%ld\n", 
                    getpid(), msg.start_pos, msg.end_pos);

                FILE *f = fopen(filename, "r");
                if (f) {
                    gettimeofday(&msg.start_time, NULL);
                    
                    fseek(f, msg.start_pos, SEEK_SET);
                    size_t bytes_read = fread(buffer, 1, 
                        msg.end_pos - msg.start_pos, f);
                    buffer[bytes_read] = '\0';

                    off_t last_complete = find_last_complete_line(buffer, bytes_read);
                    msg.bytes_processed = last_complete;
                    
                    struct timeval process_start, process_end;
                    gettimeofday(&process_start, NULL);
                    
                    char *line = strtok(buffer, "\n");
                    int lines_found = 0;
                    while (line) {
                        if (regexec(&regex, line, 0, NULL, 0) == 0) {
                            printf("-----------COINCIDENCIA:  %s\n", line);
                            lines_found++;
                        }
                        line = strtok(NULL, "\n");
                    }

                    gettimeofday(&process_end, NULL);
                    gettimeofday(&msg.end_time, NULL);
                    
                    msg.type = REPORT_RESULT;
                    msg.process_time = time_diff(process_start, process_end);
                    msg.block_time = time_diff(msg.start_time, msg.end_time);
                    msg.lines_found = lines_found;
                    
                    write(request_pipe[1], &msg, sizeof(msg));

                    printf("[HIJO %d] Bloque procesado - Líneas encontradas: %d\n", 
                           getpid(), lines_found);
                    
                    fclose(f);
                }
            }

            close(request_pipe[1]);
            close(assign_pipe[0]);
            exit(0);
        }
    }

    // Proceso padre
    close(request_pipe[1]);
    close(assign_pipe[0]);

    off_t next_pos = 0;
    int active_children = num_processes;

    printf("[PADRE] Iniciando distribución de trabajo\n");
    print_separator();

    while (active_children > 0) {
        struct message msg;
        read(request_pipe[0], &msg, sizeof(msg));

        if (msg.type == REQUEST_WORK) {
            printf("[PADRE] Solicitud recibida del proceso %d\n", msg.pid);
            
            if (next_pos < file_size) {
                if (!is_process_turn(tc, msg.pid, num_processes)) {
                    msg.type = WAIT_TURN;
                    write(assign_pipe[1], &msg, sizeof(msg));
                    printf("[PADRE] Proceso %d debe esperar su turno\n", msg.pid);
                    continue;
                }

                msg.type = ASSIGN_BLOCK;
                msg.start_pos = next_pos;
                msg.end_pos = next_pos + BUFFER_SIZE;
                if (msg.end_pos > file_size) msg.end_pos = file_size;
                next_pos = msg.end_pos;
                
                printf("[PADRE] Asignando bloque %ld-%ld al proceso %d\n",
                    msg.start_pos, msg.end_pos, msg.pid);
                
                write(assign_pipe[1], &msg, sizeof(msg));
            } else {
                msg.type = FINISH_WORK;
                write(assign_pipe[1], &msg, sizeof(msg));
                active_children--;
                printf("[PADRE] Enviando señal de término al proceso %d\n", msg.pid);
            }
        } else if (msg.type == REPORT_RESULT) {
            printf("[PADRE]  Registrando resultado del proceso %d\n", msg.pid);
            write_log_entry(log_file, &msg);
            
            // Actualizar estadísticas
            performance_stats.total_time += msg.process_time;
            performance_stats.bytes_processed += msg.bytes_processed;
            performance_stats.total_matches += msg.lines_found;
            
            if (msg.block_time > performance_stats.max_time) 
                performance_stats.max_time = msg.block_time;
            if (msg.block_time < performance_stats.min_time) 
                performance_stats.min_time = msg.block_time;
        }
    }

    for (int i = 0; i < num_processes; i++) {
        wait(NULL);
    }

    // Registrar tiempo final
    gettimeofday(&performance_stats.end_time, NULL);

    // Analizar y mostrar resultados
    analizar_rendimiento(&performance_stats, num_processes, file_size);

    // Limpieza
    close(request_pipe[0]);
    close(assign_pipe[1]);
    fclose(log_file);
    regfree(&regex);
    free(tc->process_order);
    free(tc);

    return 0;
}