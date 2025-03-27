#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

void* thread_function(void* arg) {
    int thread_num = *(int*)arg;
    cpu_set_t cpuset;
    
    // Configura l'affinity del thread
    CPU_ZERO(&cpuset);               // Inizializza un set vuoto
    CPU_SET(thread_num, &cpuset);    // Aggiunge il core specifico al set
    
    // Applica l'affinity al thread corrente
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("Impossibile impostare l'affinità");
    }
    
    // Verifica su quale core sta effettivamente girando il thread in question
    int actual_core = sched_getcpu();
    printf("Thread %d in esecuzione sul core %d\n", thread_num, actual_core);
    
    // Simula un workload
    for (int i = 0; i < 10; i++) {
        printf("Thread %d: lavoro %d/3\n", thread_num, i+1);
        sleep(1);
    }
    
    return NULL;
}

int main() {
    pthread_t thread1, thread2;
    int core1 = 0;  // Fissa il primo thread sul core 0
    int core2 = 1;  // Fissa il secondo thread sul core 1
    
    printf("Creazione thread sul core %d e %d\n", core1, core2);
    
    // Crea il primo thread
    if (pthread_create(&thread1, NULL, thread_function, &core1) != 0) {
        perror("Errore creazione thread 1");
        return 1;
    }
    
    // Crea il secondo thread
    if (pthread_create(&thread2, NULL, thread_function, &core2) != 0) {
        perror("Errore creazione thread 2");
        return 1;
    }
    
    // Attesa terminazione thread
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    
    printf("Tutti i thread hanno completato\n");
    return 0;
}
