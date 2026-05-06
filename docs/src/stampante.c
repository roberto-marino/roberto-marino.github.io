#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#define N_UTENTI 6

pthread_mutex_t stampante = PTHREAD_MUTEX_INITIALIZER;

void *utente(void *arg) {
    int id = *(int *)arg;

    /* TODO: costruire il timeout (ora corrente + 3 secondi)
     *       usando clock_gettime(CLOCK_REALTIME, ...)        */

    /* TODO: tentare l'acquisizione con pthread_mutex_timedlock()
     *       - se rc == 0:         stampare, sleep(2), unlock
     *       - se rc == ETIMEDOUT: stampare messaggio di rinuncia
     *       Ricorda: non chiamare unlock se il lock non e' stato
     *       acquisito.                                           */

    printf("Utente %d: ???\n", id);
    return NULL;
}

int main(void) {
    pthread_t threads[N_UTENTI];
    int ids[N_UTENTI];
    for (int i = 0; i < N_UTENTI; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, utente, &ids[i]);
        usleep(200000);              /* un utente ogni 0.2 s */
    }
    for (int i = 0; i < N_UTENTI; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&stampante);
    return 0;
}
