#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N_SPORTELLI  4
#define N_OPERAZIONI 100000

int saldo = 100000;  /* saldo iniziale in euro */

/* TODO: dichiarare pthread_mutex_t mtx e inizializzarlo
 *       con PTHREAD_MUTEX_INITIALIZER                    */

void *sportello(void *arg) {
    (void)arg;
    for (int i = 0; i < N_OPERAZIONI; i++) {
        int importo = (rand() % 100) + 1;

        /* TODO: acquisire il lock */

        if (i % 2 == 0) {
            saldo += importo;               /* deposito  */
        } else {
            if (saldo >= importo)           /* prelievo  */
                saldo -= importo;
        }

        /* TODO: rilasciare il lock */
    }
    return NULL;
}

int main(void) {
    pthread_t threads[N_SPORTELLI];
    int ids[N_SPORTELLI];
    for (int i = 0; i < N_SPORTELLI; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, sportello, &ids[i]);
    }
    for (int i = 0; i < N_SPORTELLI; i++)
        pthread_join(threads[i], NULL);
    printf("Saldo finale: %d euro\n", saldo);

    /* TODO: distruggere il mutex */

    return 0;
}
