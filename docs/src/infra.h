/* infra.h — infrastruttura condivisa per HO #12
 *
 * Fornisce:
 *   - struttura Msg e costanti MSG_*
 *   - coda di messaggi thread-safe (Queue)
 *   - array globale queues[N], una coda per nodo
 *   - send_msg(dst, m) / recv_msg(id)
 *
 * Non modificare questo file.
 * Compila sempre con -lpthread.
 */

#ifndef INFRA_H
#define INFRA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* Costanti */
#define N      3        /* numero di nodi                           */
#define QSIZE  256      /* capacità massima di ogni coda            */

/* Tipi di messaggio */
#define MSG_DATA    1   /* messaggio dati generico   (es. 1 e 2)    */
#define MSG_REQUEST 2   /* richiesta sezione critica (es. 3)        */
#define MSG_ACK     3   /* ACK a una REQUEST         (es. 3)        */
#define MSG_RELEASE 4   /* rilascio sezione critica  (es. 3)        */

/* Struttura messaggio */
typedef struct {
    int type;           /* uno dei MSG_* sopra                      */
    int from;           /* ID mittente (0 .. N-1)                   */
    int ts;             /* timestamp Lamport                        */
    int vec[N];         /* timestamp vettoriale                     */
    int payload;        /* dato applicativo opzionale               */
} Msg;

/* Coda FIFO thread-safe */
typedef struct {
    Msg             buf[QSIZE];
    int             head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  ready;
} Queue;

/* Una coda per nodo: queues[i] raccoglie i messaggi diretti a P_i  */
Queue queues[N];

/* Funzioni di coda */

static inline void q_init(Queue *q) {
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->ready, NULL);
}

/* Deposita m in coda; blocca se la coda è piena */
static inline void q_push(Queue *q, Msg m) {
    pthread_mutex_lock(&q->lock);
    while (q->count == QSIZE)
        pthread_cond_wait(&q->ready, &q->lock);
    q->buf[q->tail] = m;
    q->tail = (q->tail + 1) % QSIZE;
    q->count++;
    pthread_cond_broadcast(&q->ready);
    pthread_mutex_unlock(&q->lock);
}

/* Preleva il prossimo messaggio; blocca se la coda è vuota */
static inline Msg q_pop(Queue *q) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0)
        pthread_cond_wait(&q->ready, &q->lock);
    Msg m = q->buf[q->head];
    q->head = (q->head + 1) % QSIZE;
    q->count--;
    pthread_cond_broadcast(&q->ready);
    pthread_mutex_unlock(&q->lock);
    return m;
}

/* API pubblica */

/* Invia il messaggio m al nodo dst */
static inline void send_msg(int dst, Msg m) {
    q_push(&queues[dst], m);
}

/* Ricevi il prossimo messaggio per il nodo id (bloccante) */
static inline Msg recv_msg(int id) {
    return q_pop(&queues[id]);
}

/* Inizializza tutte le N code */
static inline void infra_init(void) {
    for (int i = 0; i < N; i++)
        q_init(&queues[i]);
}

#endif /* INFRA_H */
