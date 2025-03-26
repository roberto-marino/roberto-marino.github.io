#include <stdio.h>
#include <pthread.h>

void* print_number(void* arg) {
    int num = *(int*)arg;
    printf("Numero ricevuto: %d\n", num);
    return NULL;
}

int main() {
    pthread_t thread;
    int number = 42;
    
    pthread_create(&thread, NULL, print_number, &number);
    pthread_join(thread, NULL);
    
    return 0;
}
