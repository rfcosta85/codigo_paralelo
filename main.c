#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_OPS 1300
#define MAX_MACHINES 1300
#define MAX_THREADS 8  // Pode ser ajustado conforme o número de cores

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
    Job *jobs;
    int start_job;
    int end_job;
    int *machines_available;
    pthread_mutex_t *machine_mutexes;
    int thread_id;
} ThreadArgs;

int max(int a, int b) { return a > b ? a : b; }

// === Etapa 1: Partitioning ===
// Cada thread processará um subconjunto de jobs (dividido por índice)
void* process_jobs(void* args) {
    ThreadArgs *targs = (ThreadArgs*) args;

    for (int j = targs->start_job; j < targs->end_job; j++) {
        int current_time = 0;
        for (int o = 0; o < targs->jobs[j].num_ops; o++) {
            int m = targs->jobs[j].ops[o].machine;

            // === Etapa 2: Communication ===
            // Acesso protegido ao vetor compartilhado `machines_available`
            pthread_mutex_lock(&targs->machine_mutexes[m]);
            int earliest_start = max(current_time, targs->machines_available[m]);
            targs->jobs[j].ops[o].start_time = earliest_start;
            targs->jobs[j].ops[o].end_time = earliest_start + targs->jobs[j].ops[o].duration;
            targs->machines_available[m] = targs->jobs[j].ops[o].end_time;
            pthread_mutex_unlock(&targs->machine_mutexes[m]);

            current_time = targs->jobs[j].ops[o].end_time;
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <dadosDeEntrada.txt> <dadosDeSaida.txt>\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "r");
    FILE *fout = fopen(argv[2], "w");
    if (!fin || !fout) {
        perror("Erro ao abrir o arquivo");
        return 1;
    }

    int num_jobs, num_machines;
    if (fscanf(fin, "%d %d", &num_jobs, &num_machines) != 2) {
        fprintf(stderr, "Erro ao ler o número de jobs e máquinas.\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    Job *jobs = malloc(num_jobs * sizeof(Job));
    if (!jobs) {
        fprintf(stderr, "Erro de alocação de memória.\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    for (int j = 0; j < num_jobs; j++) {
        jobs[j].num_ops = num_machines;
        for (int o = 0; o < num_machines; o++) {
            if (fscanf(fin, "%d %d", &jobs[j].ops[o].machine, &jobs[j].ops[o].duration) != 2) {
                fprintf(stderr, "Erro ao ler dados do job %d, operação %d.\n", j, o);
                free(jobs);
                fclose(fin);
                fclose(fout);
                return 1;
            }
        }
    }

    fclose(fin);

    // === Etapa 3: Agglomeration ===
    // Alocar recursos compartilhados para todas as threads
    int *machines_available = calloc(MAX_MACHINES, sizeof(int));
    pthread_mutex_t machine_mutexes[MAX_MACHINES];
    for (int i = 0; i < MAX_MACHINES; i++)
        pthread_mutex_init(&machine_mutexes[i], NULL);

    pthread_t threads[MAX_THREADS];
    ThreadArgs args[MAX_THREADS];

    int jobs_per_thread = num_jobs / MAX_THREADS;
    int remainder = num_jobs % MAX_THREADS;

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // === Etapa 4: Mapping ===
    for (int i = 0; i < MAX_THREADS; i++) {
        args[i].jobs = jobs;
        args[i].start_job = i * jobs_per_thread + (i < remainder ? i : remainder);
        args[i].end_job = args[i].start_job + jobs_per_thread + (i < remainder ? 1 : 0);
        args[i].machines_available = machines_available;
        args[i].machine_mutexes = machine_mutexes;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, process_jobs, &args[i]);
    }

    for (int i = 0; i < MAX_THREADS; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    // Escreve a solução no arquivo de saída
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
    printf("Tempo de execucao (s): %.6f\n", elapsed);

    // Liberação de recursos
    for (int i = 0; i < MAX_MACHINES; i++)
        pthread_mutex_destroy(&machine_mutexes[i]);
    free(machines_available);
    free(jobs);
    fclose(fout);

    return 0;
}
