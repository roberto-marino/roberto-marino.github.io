/* es3_mutex.c — Esercizio 3: Mutex Distribuito di Lamport
 *
 * Scenario: tre nodi P0, P1, P2 vogliono entrare ciascuno una volta
 * in una sezione critica, usando il protocollo di Lamport (1978).
 *
 * Ogni nodo:
 *   1. chiama request_sc(id)   — aspetta di poter entrare
 *   2. esegue la sezione critica (simulata con una stampa + sleep)
 *   3. chiama release_sc(id)   — notifica gli altri
 *
 * Tutti e tre mandano REQUEST quasi simultaneamente: la coppia
 * <ts, id> determina l'ordine di ingresso in modo deterministico.
 *
 * Strutture dati FORNITE (non modificare):
 *   - clk[N]      clock di Lamport per nodo
 *   - pq[N]       coda di priorità delle richieste pendenti
 *   - ack_cnt[N]  numero di ACK ricevuti per la richiesta corrente
 *   - my_ts[N]    timestamp della propria REQUEST corrente
 *
 * Funzioni FORNITE (non modificare):
 *   - pq_insert, pq_remove, pq_is_top, pq_print
 *   - lc_tick(id)        R1: incrementa clk[id], restituisce il nuovo valore
 *   - lc_update(id, ts)  R3: clk[id] = max(clk[id], ts) + 1
 *
 * Compila:  gcc -Wall -o es3 es3_mutex.c -lpthread
 * Esegui:   ./es3
 */

#include "infra.h"

/* ════════════════════════════════════════════════════════════════════
 * STRUTTURE DATI — non modificare
 * ════════════════════════════════════════════════════════════════════ */

/* Coda di priorità delle richieste pendenti (ordinata per <ts, id>) */
typedef struct { int ts; int from; } Req;
typedef struct { Req items[N + 1]; int size; } PQueue;

static int    clk[N];          /* Lamport clock per ogni nodo       */
static PQueue pq[N];           /* coda di priorità per ogni nodo    */
static int    ack_cnt[N];      /* ACK ricevuti per la req. corrente */
static int    my_ts[N];        /* timestamp della propria REQUEST   */

/* ── Coda di priorità ───────────────────────────────────────────── */

static void pq_insert(PQueue *q, int ts, int from) {
    int i = q->size++;
    q->items[i] = (Req){ts, from};
    for (; i > 0; i--) {
        Req a = q->items[i-1], b = q->items[i];
        if (a.ts > b.ts || (a.ts == b.ts && a.from > b.from)) {
            q->items[i-1] = b; q->items[i] = a;
        } else break;
    }
}

static void pq_remove(PQueue *q, int from) {
    for (int i = 0; i < q->size; i++) {
        if (q->items[i].from == from) {
            for (int j = i; j < q->size - 1; j++)
                q->items[j] = q->items[j+1];
            q->size--;
            return;
        }
    }
}

/* Restituisce 1 se la richiesta (ts, from) è in cima alla coda */
static int pq_is_top(PQueue *q, int ts, int from) {
    return q->size > 0
        && q->items[0].ts   == ts
        && q->items[0].from == from;
}

static void pq_print(int id) {
    printf("[P%d] coda: ", id);
    for (int i = 0; i < pq[id].size; i++)
        printf("(%d,%d) ", pq[id].items[i].ts, pq[id].items[i].from);
    printf("\n");
}

/* ── Lamport clock helpers ──────────────────────────────────────── */

/* R1/R2: incrementa il clock, restituisce il nuovo valore */
static int lc_tick(int id) {
    return ++clk[id];
}

/* R3: aggiorna il clock alla ricezione di un messaggio con ts */
static void lc_update(int id, int ts) {
    if (ts > clk[id]) clk[id] = ts;
    clk[id]++;
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 1 — process_message(id, m)
 *
 * Gestisce un messaggio in arrivo per il nodo id secondo il tipo:
 *
 * MSG_REQUEST (from=j, ts=t):
 *   - aggiorna il clock con lc_update(id, m.ts)
 *   - inserisce la richiesta (m.ts, m.from) in pq[id]
 *   - costruisce e invia un ACK a m.from:
 *       type=MSG_ACK, from=id, ts=clk[id]
 *
 * MSG_ACK:
 *   - aggiorna il clock con lc_update(id, m.ts)
 *   - incrementa ack_cnt[id]
 *
 * MSG_RELEASE (from=j):
 *   - aggiorna il clock con lc_update(id, m.ts)
 *   - rimuove la richiesta di m.from da pq[id] con pq_remove()
 * ════════════════════════════════════════════════════════════════════ */
static void process_message(int id, Msg m) {
    /* TODO */
    (void)id; (void)m;
    (void)pq_print;   /* rimuovi quando usi pq_print */
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 2 — request_sc(id)
 *
 * Il nodo id vuole entrare nella sezione critica:
 *
 * 1. Incrementa il clock: my_ts[id] = lc_tick(id)
 * 2. Invia REQUEST a tutti gli altri nodi j != id:
 *       Msg con type=MSG_REQUEST, from=id, ts=my_ts[id]
 * 3. Inserisce la propria richiesta in pq[id]:
 *       pq_insert(&pq[id], my_ts[id], id)
 * 4. Azzera ack_cnt[id] = 0
 * 5. Attende finché NON valgono entrambe le condizioni:
 *       (a) pq_is_top(&pq[id], my_ts[id], id) == 1
 *       (b) ack_cnt[id] == N - 1
 *    Mentre aspetta, chiama process_message(id, recv_msg(id))
 *    per gestire i messaggi in arrivo.
 *
 * Al termine, il nodo può entrare in SC.
 * ════════════════════════════════════════════════════════════════════ */
static void request_sc(int id) {
    /* TODO */
    (void)id;
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 3 — release_sc(id)
 *
 * Il nodo id esce dalla sezione critica:
 *
 * 1. Rimuove la propria richiesta da pq[id]:
 *       pq_remove(&pq[id], id)
 * 2. Incrementa il clock: lc_tick(id)
 * 3. Invia RELEASE a tutti gli altri nodi j != id:
 *       Msg con type=MSG_RELEASE, from=id, ts=clk[id]
 * ════════════════════════════════════════════════════════════════════ */
static void release_sc(int id) {
    /* TODO */
    (void)id;
}

/* ── Thread dei nodi ────────────────────────────────────────────── */

static void *node(void *arg) {
    int id = *(int *)arg;

    request_sc(id);

    /* === SEZIONE CRITICA === */
    printf("[P%d] >>> ENTRO in sezione critica  (ts richiesta = %d)\n",
           id, my_ts[id]);
    usleep(50000);   /* simula lavoro: 50 ms */
    printf("[P%d] <<< ESCO  da sezione critica\n", id);
    /* ====================== */

    release_sc(id);
    return NULL;
}

/* ── main ───────────────────────────────────────────────────────── */

int main(void) {
    infra_init();
    memset(clk,     0, sizeof(clk));
    memset(pq,      0, sizeof(pq));
    memset(ack_cnt, 0, sizeof(ack_cnt));
    memset(my_ts,   0, sizeof(my_ts));

    int ids[N];
    pthread_t t[N];
    for (int i = 0; i < N; i++) {
        ids[i] = i;
        pthread_create(&t[i], NULL, node, &ids[i]);
    }
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);

    printf("\nTerminazione pulita — safety verificata se ENTRO appare\n");
    printf("una sola volta per volta, fairness se l'ordine rispetta <ts,id>.\n");
    return 0;
}
