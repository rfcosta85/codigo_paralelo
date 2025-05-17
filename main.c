#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#define MAX_JOBS 1000000
#define MAX_OPS 100
#define MAX_MACHINES 64

// ===========================
// ESTRUTURAS
// ===========================

typedef struct {
    int machine;
    int duration;
    int start_time;
    int end_time;
} Operation;

typedef struct {
    Operation ops[MAX_OPS];
    int num_ops;
} Job;

typedef struct {
    int tid;
    int start;
    int end;
} ThreadArgs;

// ===========================
// VARIÁVEIS GLOBAIS
// ===========================

Job jobs[MAX_JOBS];
int machines_available[MAX_MACHINES];
pthread_mutex_t machine_mutex[MAX_MACHINES];

int num_jobs, num_machines;

// ===========================
// FUNÇÕES
// ===========================

// Auxiliar para máximo entre dois inteiros
int max(int a, int b) {
    return (a > b) ? a : b;
}

// ===========================
// FASES DE FOSTER APLICADAS
// ===========================

// (1) Particionamento: cada thread lida com uma fatia dos jobs
// (2) Comunicação: threads sincronizam via mutex por máquina
void *schedule_jobs(void *args) {
    ThreadArgs *targs = (ThreadArgs *) args;

        // Usado para validar se as quantidades de threads que informei como argumento, realmente são utilizadas
    printf("Thread %d executando jobs de %d ate %d\n", targs->tid, targs->start, targs->end - 1);

    for (int j = targs->start; j < targs->end; j++) {
        int current_time = 0;
        for (int o = 0; o < jobs[j].num_ops; o++) {
            int m = jobs[j].ops[o].machine;
            int d = jobs[j].ops[o].duration;

            pthread_mutex_lock(&machine_mutex[m]);

            int earliest_start = max(current_time, machines_available[m]);
            jobs[j].ops[o].start_time = earliest_start;
            jobs[j].ops[o].end_time = earliest_start + d;
            machines_available[m] = jobs[j].ops[o].end_time;

            pthread_mutex_unlock(&machine_mutex[m]);

            current_time = jobs[j].ops[o].end_time;
        }
    }

    return NULL;
}

// ===========================
// MAIN
// ===========================

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <entrada.txt> <saida.txt> <num_threads.txt>\n", argv[0]);
        return 1;
    }

    // Lê número de threads
    FILE *fthreads = fopen(argv[3], "r");
    if (!fthreads) {
        perror("Erro ao abrir ficheiro de threads");
        return 1;
    }

    char thread_str[64];
    if (!fgets(thread_str, sizeof(thread_str), fthreads)) {
        fprintf(stderr, "Erro ao ler número de threads.\n");
        fclose(fthreads);
        return 1;
    }
    fclose(fthreads);

    long num_threads = strtol(thread_str, NULL, 10);
    if (num_threads <= 0 || num_threads > MAX_JOBS) {
        fprintf(stderr, "Número inválido de threads: %ld\n", num_threads);
        return 1;
    }

    // Lê os dados de entrada
    FILE *fin = fopen(argv[1], "r");
    FILE *fout = fopen(argv[2], "w");
    if (!fin || !fout) {
        perror("Erro ao abrir arquivos");
        return 1;
    }

    fscanf(fin, "%d %d", &num_jobs, &num_machines);
    for (int j = 0; j < num_jobs; j++) {
        jobs[j].num_ops = num_machines;
        for (int o = 0; o < num_machines; o++) {
            fscanf(fin, "%d %d", &jobs[j].ops[o].machine, &jobs[j].ops[o].duration);
        }
    }
    fclose(fin);

    // Inicializa mutex e máquinas
    memset(machines_available, 0, sizeof(machines_available));
    for (int i = 0; i < num_machines; i++) {
        pthread_mutex_init(&machine_mutex[i], NULL);
    }

    // (3) Aglomeração: agrupamos operações por job para reduzir sincronização
    // (4) Mapeamento: dividimos jobs em chunks e mapeamos para threads

    pthread_t threads[num_threads];
    ThreadArgs args[num_threads];
    int chunk_size = (num_jobs + num_threads - 1) / num_threads;

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int t = 0; t < num_threads; t++) {
        args[t].tid = t;
        args[t].start = t * chunk_size;
        args[t].end = (t + 1) * chunk_size > num_jobs ? num_jobs : (t + 1) * chunk_size;
        pthread_create(&threads[t], NULL, schedule_jobs, &args[t]);
    }

    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    // Escreve saída
    int makespan = 0;
    for (int j = 0; j < num_jobs; j++) {
        fprintf(fout, "Job %d:\n", j);
        for (int o = 0; o < jobs[j].num_ops; o++) {
            Operation *op = &jobs[j].ops[o];
            fprintf(fout, "  Op %d (M%d): start=%d, end=%d\n", o, op->machine, op->start_time, op->end_time);
            if (op->end_time > makespan) {
                makespan = op->end_time;
            }
        }
    }
    fprintf(fout, "Makespan: %d\n", makespan);
    fclose(fout);

    printf("Tempo de execucao (s): %.6f\n", elapsed);
    return 0;
}
