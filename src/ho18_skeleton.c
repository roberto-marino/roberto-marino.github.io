/* ================================================================
 * ho18_skeleton.c  --  Hands-On #18
 * Algoritmi distribuiti in C: processi, pipe e sincronizzazione
 *
 * Universita' di Messina -- Reti e Sistemi Distribuiti Mod.B
 *
 * Compilazione:
 *   gcc -Wall -Wextra -o ho18 ho18_skeleton.c -lm && ./ho18
 * ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <string.h>

/* -- Costanti -------------------------------------------------------- */
#define N        5          /* numero di nodi / processori              */
#define SOURCE   0          /* nodo sorgente per entrambi gli algoritmi */
#define ROUNDS   N          /* round totali (n e' sufficiente per BF)   */
#define NULL_MSG (-1.0)     /* sentinella: nessun messaggio             */
#define INF      (1e18)     /* infinito per Bellman-Ford                */
#define NO_PAR   (-1)       /* nessun parent                            */

/* -- Pipe globali ---------------------------------------------------- */
/* msg_pipe[i][j][2]: canale da i a j
 *   i scrive su msg_pipe[i][j][1]
 *   j legge da  msg_pipe[i][j][0]
 *
 * ack_pipe[i][j][2]: ack di ritorno da j verso i
 *   j scrive su ack_pipe[i][j][1]  (dopo aver letto il messaggio di i)
 *   i legge da  ack_pipe[i][j][0]  (prima di scrivere il round successivo) */
int msg_pipe[N][N][2];
int ack_pipe[N][N][2];

/* -- Topologia ------------------------------------------------------- */
/* adj[i][j] = peso dell'arco i->j  (0.0 = arco assente)              */
double adj[N][N] = {
    /* ============================================================
     * TODO -- Task 1.1
     * Inserire i pesi degli archi secondo il grafo dell'HO-18.
     * Archi:  0->1:2  0->2:5  1->2:1  1->3:4  2->3:2
     *         2->4:6  3->4:1  3->0:3  4->1:1
     * ============================================================ */
    {0, 0, 0, 0, 0},   /* riga 0 */
    {0, 0, 0, 0, 0},   /* riga 1 */
    {0, 0, 0, 0, 0},   /* riga 2 */
    {0, 0, 0, 0, 0},   /* riga 3 */
    {0, 0, 0, 0, 0},   /* riga 4 */
};

/* -- Strutture di stato ---------------------------------------------- */

/* Stato di ogni processore nell'algoritmo Flooding.
 * Corrisponde a w = (parent, data, snd_flag) del modello formale.
 *   parent   : indice del predecessore nel BFS tree (-1 = nessuno)
 *   data     : 0 = token non ancora ricevuto, 1 = token ricevuto
 *   snd_flag : 1 = trasmettere questo round, 0 = non trasmettere    */
typedef struct {
    int parent;
    int data;
    int snd_flag;
} FloodState;

/* Stato di ogni processore nell'algoritmo Bellman-Ford distribuito.
 * Corrisponde a w = (parent, dist) del modello formale.
 *   parent : indice del predecessore nel shortest-path tree
 *   dist   : stima corrente della distanza pesata dalla sorgente     */
typedef struct {
    int    parent;
    double dist;
} BFState;

/* ================================================================
 * FUNZIONI FORNITE -- non modificare
 * ================================================================ */

/* -- create_pipes() ----------------------------------------------- */
void create_pipes(void) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            if (pipe(msg_pipe[i][j]) < 0) { perror("pipe msg"); exit(1); }
            if (pipe(ack_pipe[i][j]) < 0) { perror("pipe ack"); exit(1); }
        }
}

/* -- close_unused_fds() ------------------------------------------- */
void close_unused_fds(int id) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            if (i == id) {
                close(msg_pipe[id][j][0]);   /* non legge dalla propria pipe */
                close(ack_pipe[id][j][1]);   /* non scrive ack a se stesso  */
            } else if (j == id) {
                close(msg_pipe[i][id][1]);   /* non scrive sulla pipe altrui */
                close(ack_pipe[i][id][0]);   /* non legge ack da se stesso  */
            } else {
                close(msg_pipe[i][j][0]); close(msg_pipe[i][j][1]);
                close(ack_pipe[i][j][0]); close(ack_pipe[i][j][1]);
            }
        }
    }
}

/* -- I/O sulle pipe ----------------------------------------------- */
void send_double(int fd, double val) {
    if (write(fd, &val, sizeof(double)) != sizeof(double))
        { perror("write"); exit(1); }
}

double recv_double(int fd) {
    double val;
    if (read(fd, &val, sizeof(double)) != sizeof(double))
        { perror("read"); exit(1); }
    return val;
}

void send_ack(int fd) { char c = 1; write(fd, &c, 1); }
void recv_ack(int fd) { char c;     read(fd,  &c, 1); }

/* ================================================================
 * FUNZIONI DA IMPLEMENTARE
 * ================================================================ */

/* -- flood_msg() -------------------------------------------------- */
/*
 * Funzione msg(w, i) del modello formale per il Flooding.
 *
 * Parametri:
 *   id       : UID del processore corrente
 *   s        : stato corrente (parent, data, snd_flag)
 *   neighbor : UID del vicino destinatario
 *
 * Restituisce:
 *   1.0       se snd_flag=1 e neighbor != parent  (trasmette il token)
 *   NULL_MSG  altrimenti
 */
double flood_msg(int id, FloodState *s, int neighbor) {
    /* ============================================================
     * TODO -- Task 1.2
     * ============================================================ */
    (void)id; (void)s; (void)neighbor;
    return NULL_MSG;
}

/* -- flood_stf() -------------------------------------------------- */
/*
 * Funzione stf(w, y) del modello formale per il Flooding.
 *
 * Parametri:
 *   id       : UID del processore corrente
 *   s        : stato corrente, modificato in-place
 *   in_msgs  : in_msgs[i] = messaggio ricevuto da i (NULL_MSG se assente)
 *
 * Tre casi:
 *   data=0 e tutti NULL_MSG  -> nessun aggiornamento
 *   data=0 e almeno un msg   -> parent = UID minore tra i mittenti,
 *                               data=1, snd_flag=1
 *   data=1                   -> snd_flag=0  (non ritrasmettere piu')
 */
void flood_stf(int id, FloodState *s, double in_msgs[N]) {
    /* ============================================================
     * TODO -- Task 1.3
     * ============================================================ */
    (void)id; (void)s; (void)in_msgs;
}

/* -- bf_msg() ----------------------------------------------------- */
/*
 * Funzione msg(w, i) del modello formale per Bellman-Ford distribuito.
 *
 * Parametri:
 *   id       : UID del processore corrente
 *   s        : stato corrente (parent, dist)
 *   round    : round corrente (0-indexed)
 *   neighbor : UID del vicino destinatario (non usato nel BF standard)
 *
 * Restituisce:
 *   s->dist   se round < N   (propaga la stima corrente)
 *   NULL_MSG  altrimenti
 */
double bf_msg(int id, BFState *s, int round, int neighbor) {
    /* ============================================================
     * TODO -- Task 2.1
     * ============================================================ */
    (void)id; (void)s; (void)round; (void)neighbor;
    return NULL_MSG;
}

/* -- bf_stf() ----------------------------------------------------- */
/*
 * Funzione stf(w, y) del modello formale per Bellman-Ford distribuito.
 *
 * Per ogni in-neighbor i con in_msgs[i] != NULL_MSG:
 *   candidato = in_msgs[i] + adj[i][id]
 *   se candidato < s->dist:  aggiorna dist e parent
 *
 * Parametri:
 *   id       : UID del processore corrente
 *   s        : stato corrente, modificato in-place
 *   in_msgs  : in_msgs[i] = dist ricevuta da i (NULL_MSG se assente)
 */
void bf_stf(int id, BFState *s, double in_msgs[N]) {
    /* ============================================================
     * TODO -- Task 2.2
     * ============================================================ */
    (void)id; (void)s; (void)in_msgs;
}

/* ================================================================
 * FUNZIONI FORNITE -- loop dei round (non modificare)
 * ================================================================ */

void run_flooding(int id) {
    FloodState s;
    s.parent   = (id == SOURCE) ? SOURCE : NO_PAR;
    s.data     = (id == SOURCE) ? 1 : 0;
    s.snd_flag = (id == SOURCE) ? 1 : 0;

    for (int r = 0; r < ROUNDS; r++) {
        /* Fase 1: msg */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            send_double(msg_pipe[id][j][1], flood_msg(id, &s, j));
        }
        /* Fase 2: raccolta + stf */
        double in_msgs[N];
        for (int i = 0; i < N; i++) in_msgs[i] = NULL_MSG;
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            in_msgs[i] = recv_double(msg_pipe[i][id][0]);
        }
        flood_stf(id, &s, in_msgs);
        /* Fase 3: ack verso in-neighbor */
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            send_ack(ack_pipe[i][id][1]);
        }
        /* Fase 4: attesa ack da out-neighbor */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            recv_ack(ack_pipe[id][j][0]);
        }
    }
    printf("[FLOOD] nodo %d: parent=%2d  token=%s\n",
           id, s.parent, s.data ? "SI" : "NO");
    fflush(stdout);
}

void run_bf(int id) {
    BFState s;
    s.parent = NO_PAR;
    s.dist   = (id == SOURCE) ? 0.0 : INF;

    for (int r = 0; r < ROUNDS; r++) {
        /* Fase 1: msg */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            send_double(msg_pipe[id][j][1], bf_msg(id, &s, r, j));
        }
        /* Fase 2: raccolta + stf */
        double in_msgs[N];
        for (int i = 0; i < N; i++) in_msgs[i] = NULL_MSG;
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            in_msgs[i] = recv_double(msg_pipe[i][id][0]);
        }
        bf_stf(id, &s, in_msgs);
        /* Fase 3: ack verso in-neighbor */
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            send_ack(ack_pipe[i][id][1]);
        }
        /* Fase 4: attesa ack da out-neighbor */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            recv_ack(ack_pipe[id][j][0]);
        }
    }
    if (s.dist < INF)
        printf("[BF]    nodo %d: dist=%3.0f  parent=%2d\n", id, s.dist, s.parent);
    else
        printf("[BF]    nodo %d: non raggiungibile\n", id);
    fflush(stdout);
}

/* -- main() ------------------------------------------------------- */
int main(void) {

    /* -- Parte 1: Flooding ---------------------------------------- */
    printf("=== Flooding Algorithm (sorgente: nodo %d) ===\n", SOURCE);
    fflush(stdout);
    create_pipes();
    for (int id = 0; id < N; id++) {
        pid_t pid = fork();
        if (pid < 0)  { perror("fork"); exit(1); }
        if (pid == 0) { close_unused_fds(id); run_flooding(id); exit(0); }
    }
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            close(msg_pipe[i][j][0]); close(msg_pipe[i][j][1]);
            close(ack_pipe[i][j][0]); close(ack_pipe[i][j][1]);
        }
    for (int i = 0; i < N; i++) wait(NULL);

    /* -- Parte 2: Bellman-Ford ------------------------------------ */
    printf("\n=== Distributed Bellman-Ford (sorgente: nodo %d) ===\n", SOURCE);
    fflush(stdout);
    create_pipes();
    for (int id = 0; id < N; id++) {
        pid_t pid = fork();
        if (pid < 0)  { perror("fork"); exit(1); }
        if (pid == 0) { close_unused_fds(id); run_bf(id); exit(0); }
    }
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            close(msg_pipe[i][j][0]); close(msg_pipe[i][j][1]);
            close(ack_pipe[i][j][0]); close(ack_pipe[i][j][1]);
        }
    for (int i = 0; i < N; i++) wait(NULL);

    return 0;
}
