/* ================================================================
 * ho19_floodmax_skeleton.c  --  Hands-On #19  [SKELETON PER STUDENTI]
 * Leader Election: Floodmax Algorithm
 *
 * Universita' di Messina -- Reti e Sistemi Distribuiti Mod.B
 *
 * Compilazione:
 *   gcc -Wall -Wextra -o ho19_floodmax ho19_floodmax_skeleton.c && ./ho19_floodmax
 * ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define N        6          /* numero di nodi                          */
#define DIAM     3          /* diametro della rete (noto a priori)     */
#define ROUNDS   (DIAM + 1) /* DIAM round di msg + 1 round finale      */
#define NULL_MSG (-1.0)     /* sentinella: nessun messaggio            */
#define UNKNWN   0          /* leader non ancora determinato           */
#define IS_LDR   1          /* questo nodo e' il leader                */
#define NOT_LDR  2          /* questo nodo non e' il leader            */

int msg_pipe[N][N][2];
int ack_pipe[N][N][2];

/* Task 1.1 -- Matrice di adiacenza  */
double adj[N][N] = {
/*        0     1     2     3     4     5  */
/* 0 */ { 0.0,  1.0,  1.0,  0.0,  0.0,  0.0 },
/* 1 */ { 0.0,  0.0,  0.0,  1.0,  0.0,  0.0 },
/* 2 */ { 0.0,  0.0,  0.0,  1.0,  1.0,  0.0 },
/* 3 */ { 0.0,  0.0,  0.0,  0.0,  0.0,  1.0 },
/* 4 */ { 0.0,  0.0,  0.0,  0.0,  0.0,  1.0 },
/* 5 */ { 1.0,  1.0,  1.0,  0.0,  1.0,  0.0 },
};

/*  Struttura di stato */
/* w = (my_id, max_id, leader, round) del modello formale.
 *   my_id  : UID proprio, immutabile
 *   max_id : massimo UID visto finora
 *   leader : UNKNWN / IS_LDR / NOT_LDR
 *   round  : contatore del round corrente                          */
typedef struct {
    int my_id;
    int max_id;
    int leader;
    int round;
} FloodMaxState;

/*  Funzioni fornite  */

void create_pipes(void) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            if (pipe(msg_pipe[i][j]) < 0) { perror("pipe msg"); exit(1); }
            if (pipe(ack_pipe[i][j]) < 0) { perror("pipe ack"); exit(1); }
        }
}

void close_unused_fds(int id) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            if (i == id) {
                close(msg_pipe[id][j][0]);
                close(ack_pipe[id][j][1]);
            } else if (j == id) {
                close(msg_pipe[i][id][1]);
                close(ack_pipe[i][id][0]);
            } else {
                close(msg_pipe[i][j][0]); close(msg_pipe[i][j][1]);
                close(ack_pipe[i][j][0]); close(ack_pipe[i][j][1]);
            }
        }
    }
}

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

/*  Task 1.2 -- floodmax_msg()  */
double floodmax_msg(int id, FloodMaxState *s, int neighbor) {
    /* ============================================================
     * TODO -- Task 1.2
     * Restituisce max_id se round < DIAM, altrimenti NULL_MSG.
     * ============================================================ */
    (void)id; (void)s; (void)neighbor;
    return NULL_MSG;
}

/*  Task 1.3 -- floodmax_stf()  */
void floodmax_stf(int id, FloodMaxState *s, double in_msgs[N]) {
    (void)id;
    /* Aggiorna max_id con il massimo ricevuto. */
    for (int i = 0; i < N; i++) {
        if (in_msgs[i] == NULL_MSG) continue;
        int received = (int)in_msgs[i];
        if (received > s->max_id)
            s->max_id = received;
    }
    /* All'ultimo round determina se questo nodo e' il leader. */
    if (s->round == DIAM) {
        s->leader = (s->max_id == s->my_id) ? IS_LDR : NOT_LDR;
    }
    s->round++;
}

/*  Loop dei round (fornito)  */

void run_floodmax(int id) {
    FloodMaxState s;
    s.my_id  = id;
    s.max_id = id;
    s.leader = UNKNWN;
    s.round  = 0;

    for (int r = 0; r < ROUNDS; r++) {
        /* Fase 1: msg */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            send_double(msg_pipe[id][j][1], floodmax_msg(id, &s, j));
        }
        /* Fase 2: raccolta + stf */
        double in_msgs[N];
        for (int i = 0; i < N; i++) in_msgs[i] = NULL_MSG;
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            in_msgs[i] = recv_double(msg_pipe[i][id][0]);
        }
        floodmax_stf(id, &s, in_msgs);
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

    const char *lstr = (s.leader == IS_LDR)  ? "SI (LEADER)" :
                       (s.leader == NOT_LDR) ? "NO"         : "UNKNWN";
    printf("[FLOODMAX] nodo %d: max_id=%d  leader=%s\n",
           id, s.max_id, lstr);
    fflush(stdout);
}

/*  main()  */
int main(void) {
    printf("=== Floodmax Algorithm (N=%d, DIAM=%d) ===\n", N, DIAM);
    fflush(stdout);
    create_pipes();
    for (int id = 0; id < N; id++) {
        pid_t pid = fork();
        if (pid < 0)  { perror("fork"); exit(1); }
        if (pid == 0) { close_unused_fds(id); run_floodmax(id); exit(0); }
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
