/*
 * Hands-On #15 — Fase 1
 * Buffer circolare a singolo thread: baseline corretta.
 *
 * Compile: gcc -Wall -Wextra -o fase1 fase1.c
 * Run:     ./fase1
 */

#include <stdio.h>
#include <stdlib.h>

#define N     8
#define NITEM 16


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

int main(void) {
    Buffer b = {0};

    /*
     * Inseriamo e preleviamo a blocchi di N per dimostrare
     * che il buffer circolare gestisce correttamente il
     * wrap-around degli indici.
     */
    for (int round = 0; round < NITEM / N; round++) {
        /* Riempi il buffer */
        for (int i = 0; i < N; i++)
            buf_put(&b, round * N + i);

        /* Svuota il buffer */
        for (int i = 0; i < N; i++)
            printf("Prelevo: %d\n", buf_get(&b));
    }

    printf("Tutti gli elementi prelevati correttamente.\n");

    /*
     * Domanda fase 1, punto 3: cosa succede se chiamiamo
     * buf_get su un buffer vuoto?
     * Decommentate la riga seguente per osservarlo.
     */
    /* printf("Elemento extra: %d\n", buf_get(&b)); */

    return 0;
}
