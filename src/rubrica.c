#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define N_LETTORI 8

typedef struct { char nome[32]; char numero[16]; } Contatto;

Contatto rubrica[] = {
    {"Alice", "333-1111"},
    {"Bob",   "333-2222"},
    {"Carlo", "333-3333"},
};
int n_contatti = 3;

/* TODO: dichiarare pthread_rwlock_t rubrica_lock e inizializzarlo
 *       con PTHREAD_RWLOCK_INITIALIZER                             */

void *lettore(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < 5; i++) {
        /* TODO: acquisire il lock in lettura */

        int idx = i % n_contatti;
        printf("Lettore %d: %s -> %s\n",
               id, rubrica[idx].nome, rubrica[idx].numero);
        usleep(100000);

        /* TODO: rilasciare il lock */
    }
    return NULL;
}

void *scrittore(void *arg) {
    (void)arg;
    sleep(1);  /* lascia partire i lettori */

    /* TODO: acquisire il lock in scrittura */

    printf("Scrittore: aggiorno il numero di Bob...\n");
    strcpy(rubrica[1].numero, "333-9999");
    sleep(2);  /* simula aggiornamento lento */
    printf("Scrittore: aggiornamento completato.\n");

    /* TODO: rilasciare il lock */

    return NULL;
}

int main(void) {
    /* TODO: inizializzare rubrica_lock con pthread_rwlock_init() */

    pthread_t lettori[N_LETTORI], scr;
    int ids[N_LETTORI];
    pthread_create(&scr, NULL, scrittore, NULL);
    for (int i = 0; i < N_LETTORI; i++) {
        ids[i] = i;
        pthread_create(&lettori[i], NULL, lettore, &ids[i]);
    }
    pthread_join(scr, NULL);
    for (int i = 0; i < N_LETTORI; i++)
        pthread_join(lettori[i], NULL);

    /* TODO: distruggere rubrica_lock */

    return 0;
}
