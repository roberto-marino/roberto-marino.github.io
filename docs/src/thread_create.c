#include <stdio.h>
#include <pthread.h>

// Funzione che verrà eseguita dal thread
void* thread_function(void* arg) {
    printf("Thread in esecuzione!\n");
    return NULL;
}

int main() {
    pthread_t thread_id;
    
    // Creazione del thread
    int result = pthread_create(&thread_id, NULL, thread_function, NULL);
    
    if(result != 0) {
        perror("Errore nella creazione del thread");
        return 1;
    }
    
    // Attendere la terminazione del thread
    pthread_join(thread_id, NULL);
    
    printf("Thread terminato\n");
    return 0;
}
