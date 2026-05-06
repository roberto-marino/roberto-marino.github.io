/*
 * Hands-On #15 — Fase 4
 * Soluzione corretta con mutex + condition variables.
 *
 * Compile: gcc -Wall -Wextra -pthread -o fase4 fase4.c
 * Run:     ./fase4
 *
 * Verificare con: valgrind --tool=helgrind ./fase4
 * Misurare CPU:   ./fase4 & htop
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N     8
#define NITEM 10000


typedef struct {
    int buf[N];
    int head, tail, count;
} Buffer;

void buf_put(Buffer *b, int v) {
    b->buf[b->tail] = v;
    b->tail = (b->tail + 1) % N;
    b->count++;
}

int buf_get(Buffer *b) {
    int v = b->buf[b->head];
    b->head = (b->head + 1) % N;
    b->count--;
    return v;
}


typedef struct {
    Buffer          b;
    pthread_mutex_t mtx;
    pthread_cond_t  not_full;   /* segnalata dal consumatore */
    pthread_cond_t  not_empty;  /* segnalata dal produttore  */
} Shared;


/*
 * Produttore corretto.
 *
 * pthread_cond_wait(&not_full, &mtx) fa tre cose atomicamente:
 *   1. rilascia mtx
 *   2. sospende il thread nella coda di not_full
 *   3. al risveglio riacquisisce mtx
 *
 * L'atomicita' dei passi 1 e 2 elimina la finestra di race
 * della fase 3: non esiste un momento in cui il mutex e'
 * libero e il thread non e' ancora in attesa.
 *
 * Il while (non if) e' necessario per due ragioni:
 *   a) spurious wakeup: il kernel puo' svegliare il thread
 *      senza che nessuno abbia chiamato signal
 *   b) piu' produttori: un altro produttore potrebbe aver
 *      gia' riempito il buffer tra il signal e il risveglio
 */
static void *produttore(void *arg) {
    Shared *s = (Shared *)arg;
    for (int i = 0; i < NITEM; i++) {
        pthread_mutex_lock(&s->mtx);

        while (s->b.count == N)
            pthread_cond_wait(&s->not_full, &s->mtx);

        buf_put(&s->b, i);

        /*
         * signal (non broadcast): abbiamo inserito UN elemento,
         * quindi al massimo un consumatore puo' procedere.
         * Svegliarne di piu' sarebbe inutile: si sveglierebbero,
         * troverebbero il buffer ancora vuoto (o gia' prelevato
         * dall'altro), e tornerebbero a dormire.
         */
        pthread_cond_signal(&s->not_empty);
        pthread_mutex_unlock(&s->mtx);
    }
    return NULL;
}

/*
 * Consumatore corretto.
 * Schema speculare al produttore: stessa struttura, CV invertite.
 */
static void *consumatore(void *arg) {
    Shared *s  = (Shared *)arg;
    int errori = 0;

    for (int i = 0; i < NITEM; i++) {
        pthread_mutex_lock(&s->mtx);

        while (s->b.count == 0)
            pthread_cond_wait(&s->not_empty, &s->mtx);

        int v = buf_get(&s->b);

        pthread_cond_signal(&s->not_full);
        pthread_mutex_unlock(&s->mtx);

        /* Verifica ordine FIFO */
        if (v != i) {
            fprintf(stderr, "*** ERRORE: atteso %d, ricevuto %d\n", i, v);
            errori++;
        }
    }

    if (errori == 0)
        printf("OK: tutti i %d elementi ricevuti nell'ordine corretto.\n",
               NITEM);
    else
        printf("ERRORE: %d anomalie rilevate.\n", errori);

    return NULL;
}


int main(void) {
    Shared s = {0};
    pthread_mutex_init(&s.mtx,       NULL);
    pthread_cond_init (&s.not_full,  NULL);
    pthread_cond_init (&s.not_empty, NULL);

    pthread_t tp, tc;
    pthread_create(&tp, NULL, produttore,  &s);
    pthread_create(&tc, NULL, consumatore, &s);

    pthread_join(tp, NULL);
    pthread_join(tc, NULL);

    pthread_mutex_destroy(&s.mtx);
    pthread_cond_destroy (&s.not_full);
    pthread_cond_destroy (&s.not_empty);
    return 0;
}
