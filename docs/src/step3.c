/*
 * Hands-On #15 — Fase 3
 * Mutex aggiunto: corruzione scomparsa, ma deadlock garantito.
 *
 * Compile: gcc -Wall -Wextra -pthread -o fase3 fase3.c
 * Run:     ./fase3
 *
 * Il programma si blocca quando il buffer si riempie o si svuota.
 * Interrompere con Ctrl+C.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N     4     /* buffer piccolo: deadlock piu' rapido */
#define NITEM 32


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
} Shared;


/*
 * Produttore con mutex.
 *
 * Il mutex protegge count, head, tail: la corruzione scompare.
 * Ma quando il buffer e' pieno, il produttore entra nel while
 * TENENDO il mutex. Il consumatore non puo' acquisirlo per
 * prelevare un elemento. Nessuno dei due avanza: deadlock.
 */
static void *produttore(void *arg) {
    Shared *s = (Shared *)arg;
    for (int i = 0; i < NITEM; i++) {
        pthread_mutex_lock(&s->mtx);

        /*
         * PROBLEMA: il mutex e' tenuto qui dentro.
         * Il consumatore non puo' acquisirlo per svuotare
         * il buffer, quindi count non scendera' mai.
         * Questo loop non terminera' mai.
         */
        while (s->b.count == N) {
            printf("[produttore] buffer pieno, aspetto... "
                   "(mutex TENUTO: deadlock!)\n");
            /* nessun rilascio del mutex */
        }

        buf_put(&s->b, i);
        printf("[produttore] inserito %d (count=%d)\n", i, s->b.count);
        pthread_mutex_unlock(&s->mtx);
    }
    return NULL;
}

/*
 * Consumatore con mutex.
 *
 * Stesso problema speculare: quando il buffer e' vuoto,
 * il consumatore aspetta tenendo il mutex e il produttore
 * non puo' inserire.
 */
static void *consumatore(void *arg) {
    Shared *s = (Shared *)arg;
    for (int i = 0; i < NITEM; i++) {
        pthread_mutex_lock(&s->mtx);

        while (s->b.count == 0) {
            printf("[consumatore] buffer vuoto, aspetto... "
                   "(mutex TENUTO: deadlock!)\n");
        }

        int v = buf_get(&s->b);
        printf("[consumatore] prelevato %d (count=%d)\n", v, s->b.count);
        pthread_mutex_unlock(&s->mtx);
    }
    return NULL;
}


int main(void) {
    Shared s = {0};
    pthread_mutex_init(&s.mtx, NULL);

    pthread_t tp, tc;
    pthread_create(&tp, NULL, produttore,  &s);
    pthread_create(&tc, NULL, consumatore, &s);

    /*
     * Con N=4 il produttore riempie il buffer prima che
     * il consumatore abbia il tempo di prelevare.
     * Appena il buffer si riempie (count==N), il produttore
     * entra nel while e non rilascia piu' il mutex.
     * Il programma si blocca qui sotto senza mai tornare.
     */
    pthread_join(tp, NULL);
    pthread_join(tc, NULL);

    pthread_mutex_destroy(&s.mtx);
    printf("Fine (non raggiungeremo mai questa riga).\n");
    return 0;
}
