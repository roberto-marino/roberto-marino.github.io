/* es2_vector.c -- Esercizio 2: Vector Clock
 *
 * Stesso scenario dell'esercizio 1:
 *
 *   P0: evento interno a,  invia a P1,  evento interno b
 *   P1: evento interno c,  riceve da P0, invia a P2
 *   P2: riceve da P1,       evento interno d
 *
 * L'obiettivo e' verificare che il vector clock rilevi correttamente:
 *   la relazione causale tra send(P0->P1) e recv(P1)
 *   la relazione causale tra send(P1->P2) e recv(P2)
 *   la concorrenza tra l'evento a di P0 e l'evento c di P1
 *
 * Compila:  gcc -Wall -o es2 es2_vector.c -lpthread
 * Esegui:   ./es2
 */

#include "infra.h"

/* Vector clock: un vettore di N interi per nodo */
static int vc[N][N];   /* vc[i][k] = eventi di Pk noti a Pi */

/* Salvataggio timestamp per la verifica */
static int ts_a[N];         /* V al momento dell'evento a di P0 */
static int ts_c[N];         /* V al momento dell'evento c di P1 */
static int ts_send_p0[N];   /* V al momento di send(P0->P1)     */
static int ts_recv_p1[N];   /* V al momento di recv(P1)         */
static int ts_send_p1[N];   /* V al momento di send(P1->P2)     */
static int ts_recv_p2[N];   /* V al momento di recv(P2)         */

/* Copia il vettore clock del nodo id in dst */
static void vc_copy(int dst[N], int id) {
    for (int k = 0; k < N; k++) dst[k] = vc[id][k];
}

/* Stampa un vettore clock con etichetta */
static void vc_print(const char *label, int v[N]) {
    printf("%s [", label);
    for (int k = 0; k < N; k++) printf("%d%s", v[k], k < N-1 ? "," : "");
    printf("]\n");
}

/*
 * TODO 1 -- vc_event(id)
 *
 * Applica R1 (evento interno) sul nodo id:
 *   vc[id][id] += 1
 * Stampa:  [Pi] evento interno   V = [v0,v1,v2]
 */
static void vc_event(int id) {
    /* TODO */
    (void)id;
}

/*
 * TODO 2 -- vc_send(id, dst, payload)
 *
 * Applica R2 (invio messaggio) dal nodo id al nodo dst:
 *   vc[id][id] += 1
 *   costruisce Msg con type=MSG_DATA, from=id, vec=vc[id], payload
 *   deposita il messaggio con send_msg()
 * Stampa:  [Pi] send -> Pj   V = [v0,v1,v2]
 */
static void vc_send(int id, int dst, int payload) {
    /* TODO */
    (void)id; (void)dst; (void)payload;
}

/*
 * TODO 3 -- vc_recv(id)
 *
 * Applica R3 (ricezione messaggio) sul nodo id:
 *   m = recv_msg(id)
 *   per ogni k: vc[id][k] = max(vc[id][k], m.vec[k])
 *   vc[id][id] += 1
 * Stampa:  [Pi] recv <- Pj   V = [v0,v1,v2]
 * Restituisce il messaggio ricevuto.
 */
static Msg vc_recv(int id) {
    /* TODO */
    (void)id;
    Msg m = {0};
    return m;
}

/*
 * TODO 4 -- vc_cmp(a, b)
 *
 * Confronta due vettori clock e restituisce:
 *   -1  se a < b  (a causalmente prima di b)
 *    1  se a > b  (b causalmente prima di a)
 *    0  se a == b (identici)
 *    2  se a || b (concorrenti)
 *
 * Definizioni:
 *   a <= b  sse  per ogni k: a[k] <= b[k]
 *   a <  b  sse  a<=b  e  esiste k: a[k] < b[k]
 */
static int vc_cmp(int a[N], int b[N]) {
    /* TODO */
    (void)a; (void)b;
    return 0;
}

static const char *cmp_str(int r) {
    switch (r) {
        case -1: return "CAUSALE  (a -> b)";
        case  1: return "CAUSALE  (b -> a)";
        case  0: return "IDENTICI";
        case  2: return "CONCORRENTI";
        default: return "?";
    }
}

/* Thread dei nodi */

static void *node0(void *arg) {
    (void)arg;
    vc_event(0);  vc_copy(ts_a, 0);
    vc_send(0, 1, 42);
    vc_copy(ts_send_p0, 0);
    vc_event(0);
    return NULL;
}

static void *node1(void *arg) {
    (void)arg;
    vc_event(1);  vc_copy(ts_c, 1);
    vc_recv(1);   vc_copy(ts_recv_p1, 1);
    vc_send(1, 2, 0);
    vc_copy(ts_send_p1, 1);
    return NULL;
}

static void *node2(void *arg) {
    (void)arg;
    vc_recv(2);  vc_copy(ts_recv_p2, 2);
    vc_event(2);
    return NULL;
}

/*
 * TODO 5 -- verify()
 *
 * Chiama vc_cmp sulle tre coppie e stampa PASS o FAIL:
 *
 *   ts_a       vs ts_c        atteso: CONCORRENTI
 *   ts_send_p0 vs ts_recv_p1  atteso: CAUSALE (a -> b)
 *   ts_send_p1 vs ts_recv_p2  atteso: CAUSALE (a -> b)
 */
static void verify(void) {
    printf("\n=== Confronti vector clock ===\n");
    /* TODO */
    (void)cmp_str;
    (void)vc_print;
}

/* main */

int main(void) {
    infra_init();
    memset(vc, 0, sizeof(vc));

    pthread_t t[N];
    void *(*fns[N])(void *) = { node0, node1, node2 };
    for (int i = 0; i < N; i++)
        pthread_create(&t[i], NULL, fns[i], NULL);
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);

    verify();
    return 0;
}
