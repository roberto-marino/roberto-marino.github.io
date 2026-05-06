/*
 * Hands-On #15 — Fase 2
 * Due thread, nessuna sincronizzazione: corruzione dei dati.
 *
 * Compile: gcc -Wall -Wextra -pthread -o fase2 fase2.c
 * Run:     ./fase2
 *
 * Eseguire piu' volte: l'output cambia.
 * Eseguire con: valgrind --tool=helgrind ./fase2
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N     8
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


/*
 * Produttore: inserisce NITEM valori nel buffer.
 * Se il buffer e' pieno aspetta con busy-wait.
 * Nessun mutex: count, head, tail non sono protetti.
 */
static void *produttore(void *arg) {
    Buffer *b = (Buffer *)arg;
    for (int i = 0; i < NITEM; i++) {
        while (b->count == N)   /* busy-wait: buffer pieno */
            ;
        buf_put(b, i);
    }
    return NULL;
}

/*
 * Consumatore: preleva NITEM valori dal buffer.
 * Se il buffer e' vuoto aspetta con busy-wait.
 * Nessun mutex: stessa mancanza del produttore.
 */
static void *consumatore(void *arg) {
    Buffer *b = (Buffer *)arg;
    int ricevuti = 0;
    int errori   = 0;

    for (int i = 0; i < NITEM; i++) {
        while (b->count == 0)   /* busy-wait: buffer vuoto */
            ;
        int v = buf_get(b);
        printf("Prelevo: %d\n", v);

        /*
         * Verifica FIFO: in assenza di race condition
         * dovremmo ricevere i valori in ordine 0, 1, 2, ...
         * Se ne riceviamo uno fuori ordine o duplicato,
         * e' un segno di corruzione.
         */
        if (v != ricevuti) {
            fprintf(stderr,
                "*** CORRUZIONE: atteso %d, ricevuto %d ***\n",
                ricevuti, v);
            errori++;
        }
        ricevuti++;
    }

    if (errori == 0)
        printf("Nessuna corruzione rilevata (questa volta).\n");
    else
        printf("%d corruzione/i rilevata/e.\n", errori);

    return NULL;
}


int main(void) {
    Buffer    b  = {0};
    pthread_t tp, tc;

    pthread_create(&tp, NULL, produttore,  &b);
    pthread_create(&tc, NULL, consumatore, &b);

    pthread_join(tp, NULL);
    pthread_join(tc, NULL);

    return 0;
}
