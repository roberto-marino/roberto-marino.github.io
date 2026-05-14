/* es1_lamport.c — Esercizio 1: Clock di Lamport
 *
 * Scenario (fisso, non modificare node0/node1/node2):
 *
 *   P0: evento interno a,  invia a P1,  evento interno b
 *   P1: evento interno c,  riceve da P0, invia a P2
 *   P2: riceve da P1,       evento interno d
 *
 * Relazioni causali garantite dallo scenario:
 *   send(P0->P1)  happened-before  recv(P1)
 *   send(P1->P2)  happened-before  recv(P2)
 *
 * Evento concorrente:
 *   l'evento interno c di P1 NON è in relazione causale con a di P0
 *   (nessun messaggio li collega).
 *
 * Compila:  gcc -Wall -o es1 es1_lamport.c -lpthread
 * Esegui:   ./es1
 */

#include "infra.h"

/* ── Stato del clock di Lamport (un intero per nodo) ────────────── */
static int clk[N];  /* clk[i] è il clock corrente del nodo i       */

/* ── Struttura per salvare i timestamp da verificare ────────────── */
typedef struct { int send_p0_p1;   /* C al momento di send(P0->P1) */
                 int recv_p1;      /* C al momento di recv(P1)      */
                 int send_p1_p2;   /* C al momento di send(P1->P2) */
                 int recv_p2;      /* C al momento di recv(P2)      */
} Saved;
static Saved saved;

/* ════════════════════════════════════════════════════════════════════
 * TODO 1 — lc_event(id)
 *
 * Applica la regola R1 (evento interno) sul nodo id:
 *   clk[id] += 1
 * Stampa:  [Pi] evento interno   C = <valore>
 * Restituisce il nuovo valore del clock.
 * ════════════════════════════════════════════════════════════════════ */
static int lc_event(int id) {
    /* TODO */
    (void)id;
    return 0;
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 2 — lc_send(id, dst, payload)
 *
 * Applica la regola R2 (invio messaggio) dal nodo id al nodo dst:
 *   clk[id] += 1
 *   costruisce Msg con type=MSG_DATA, from=id, ts=clk[id], payload
 *   deposita il messaggio nella coda di dst con send_msg()
 * Stampa:  [Pi] send -> Pj   C = <valore>
 * Restituisce il timestamp allegato al messaggio.
 * ════════════════════════════════════════════════════════════════════ */
static int lc_send(int id, int dst, int payload) {
    /* TODO */
    (void)id; (void)dst; (void)payload;
    return 0;
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 3 — lc_recv(id)
 *
 * Applica la regola R3 (ricezione messaggio) sul nodo id:
 *   m = recv_msg(id)            // preleva dalla propria coda
 *   clk[id] = max(clk[id], m.ts) + 1
 * Stampa:  [Pi] recv <- Pj   C = <valore>
 * Restituisce il messaggio ricevuto.
 * ════════════════════════════════════════════════════════════════════ */
static Msg lc_recv(int id) {
    /* TODO */
    (void)id;
    Msg m = {0};
    return m;
}

/* ── Thread dei nodi (scenario fisso) ───────────────────────────── */

static void *node0(void *arg) {
    (void)arg;
    lc_event(0);                        /* evento interno a */
    saved.send_p0_p1 = lc_send(0, 1, 42); /* invia a P1   */
    lc_event(0);                        /* evento interno b */
    return NULL;
}

static void *node1(void *arg) {
    (void)arg;
    lc_event(1);                        /* evento interno c */
    Msg m = lc_recv(1);                 /* riceve da P0     */
    saved.recv_p1 = clk[1];
    (void)m;
    saved.send_p1_p2 = lc_send(1, 2, 0); /* invia a P2    */
    return NULL;
}

static void *node2(void *arg) {
    (void)arg;
    Msg m = lc_recv(2);                 /* riceve da P1     */
    saved.recv_p2 = clk[2];
    (void)m;
    lc_event(2);                        /* evento interno d */
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
 * TODO 4 — verifica della proprietà
 *
 * Dopo il join di tutti i thread, controlla che per ogni coppia
 * causale (send, recv) valga C(send) < C(recv).
 * Stampa PASS o FAIL per ciascuna coppia.
 *
 * Coppie da verificare (i valori sono già salvati in `saved`):
 *   send(P0->P1) : saved.send_p0_p1
 *   recv(P1)     : saved.recv_p1
 *   send(P1->P2) : saved.send_p1_p2
 *   recv(P2)     : saved.recv_p2
 * ════════════════════════════════════════════════════════════════════ */
static void verify(void) {
    printf("\n=== Verifica proprietà C(send) < C(recv) ===\n");
    /* TODO */
}

/* ── main ───────────────────────────────────────────────────────── */

int main(void) {
    infra_init();
    memset(clk,   0, sizeof(clk));
    memset(&saved, 0, sizeof(saved));

    pthread_t t[N];
    void *(*fns[N])(void *) = { node0, node1, node2 };
    for (int i = 0; i < N; i++)
        pthread_create(&t[i], NULL, fns[i], NULL);
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);

    verify();
    return 0;
}
