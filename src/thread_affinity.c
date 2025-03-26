#include <stdio.h>
#include <pthread.h>
#include <sched.h>

#define NUM_THREADS 2

void* thread_function(void* arg) {
    int thread_num = *(int*)arg;
    cpu_set_t cpuset;
    
    // Imposta l'affinity del thread
    CPU_ZERO(&cpuset);
    CPU_SET(thread_num, &cpuset);  // Lega il thread al core corrispondente al suo numero
    
    // Simula un carico di lavoro
    for (volatile int i = 0; i < 100000000; i++);  // Loop vuoto per consumare CPU
    
    printf("Thread %d completato\n", thread_num);
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_nums[NUM_THREADS];
    
    printf("Creazione di %d thread con affinity specifica\n", NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_nums[i] = i;
        if (pthread_create(&threads[i], NULL, thread_function, &thread_nums[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Tutti i thread hanno completato l'esecuzione\n");
    return 0;
}
