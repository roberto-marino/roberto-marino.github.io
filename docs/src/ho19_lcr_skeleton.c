/* ================================================================
 * ho19_lcr_skeleton.c  --  Hands-On #19  [SKELETON PER STUDENTI]
 * Leader Election: LCR Algorithm su anello
 *
 * Universita' di Messina -- Reti e Sistemi Distribuiti Mod.B
 *
 * Compilazione:
 *   gcc -Wall -Wextra -o ho19_lcr ho19_lcr_skeleton.c && ./ho19_lcr
 * ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define N        5          /* numero di nodi nell'anello              */
#define ROUNDS   N          /* n round sono sufficienti per LCR        */
#define NULL_MSG (-1.0)
#define UNKNWN   0
#define IS_LDR   1
#define NOT_LDR  2

int msg_pipe[N][N][2];
int ack_pipe[N][N][2];

/*  Task 2.1 -- Matrice di adiacenza (anello 0->1->2->3->4->0)  */
double adj[N][N] = {
/*        0     1     2     3     4  */
/* 0 */ { 0.0,  1.0,  0.0,  0.0,  0.0 },
/* 1 */ { 0.0,  0.0,  1.0,  0.0,  0.0 },
/* 2 */ { 0.0,  0.0,  0.0,  1.0,  0.0 },
/* 3 */ { 0.0,  0.0,  0.0,  0.0,  1.0 },
/* 4 */ { 1.0,  0.0,  0.0,  0.0,  0.0 },
};

/*  Struttura di stato  */
/* w = (my_id, max_id, leader, snd_flag) del modello formale.
 *   my_id    : UID proprio, immutabile
 *   max_id   : massimo UID visto finora
 *   leader   : UNKNWN / IS_LDR / NOT_LDR
 *   snd_flag : 1 = trasmetti questo round, 0 = taci               */
typedef struct {
    int my_id;
    int max_id;
    int leader;
    int snd_flag;
} LCRState;

/*  Funzioni fornite*/

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

/*  Task 2.2 -- lcr_msg()  */
double lcr_msg(int id, LCRState *s, int neighbor) {
    /* ============================================================
     * TODO -- Task 2.2
     * Restituisce max_id se snd_flag=1, altrimenti NULL_MSG.
     * ============================================================ */
    (void)id; (void)s; (void)neighbor;
    return NULL_MSG;
}

/*  Task 2.3 -- lcr_stf()  */
void lcr_stf(int id, LCRState *s, double in_msgs[N]) {
    /* ============================================================
     * TODO -- Task 2.3
     * Tre casi (basati su recv_id rispetto a my_id):
     *   recv_id <  my_id : snd_flag = 0  (taci)
     *   recv_id == my_id : leader = IS_LDR, snd_flag = 0
     *   recv_id >  my_id : max_id = recv_id, leader = NOT_LDR,
     *                       snd_flag = 1
     * ============================================================ */
    (void)id; (void)s; (void)in_msgs;
}

/* Loop dei round (fornito)  */

void run_lcr(int id) {
    LCRState s;
    s.my_id    = id;
    s.max_id   = id;
    s.leader   = UNKNWN;
    s.snd_flag = 1;     /* tutti partono pronti a trasmettere */

    for (int r = 0; r < ROUNDS; r++) {
        /* Fase 1: msg */
        for (int j = 0; j < N; j++) {
            if (j == id || adj[id][j] == 0.0) continue;
            send_double(msg_pipe[id][j][1], lcr_msg(id, &s, j));
        }
        /* Fase 2: raccolta + stf */
        double in_msgs[N];
        for (int i = 0; i < N; i++) in_msgs[i] = NULL_MSG;
        for (int i = 0; i < N; i++) {
            if (i == id || adj[i][id] == 0.0) continue;
            in_msgs[i] = recv_double(msg_pipe[i][id][0]);
        }
        lcr_stf(id, &s, in_msgs);
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
    printf("[LCR] nodo %d: max_id=%d  leader=%s\n", id, s.max_id, lstr);
    fflush(stdout);
}

/*  main()  */
int main(void) {
    printf("=== LCR Algorithm (anello di %d nodi) ===\n", N);
    fflush(stdout);
    create_pipes();
    for (int id = 0; id < N; id++) {
        pid_t pid = fork();
        if (pid < 0)  { perror("fork"); exit(1); }
        if (pid == 0) { close_unused_fds(id); run_lcr(id); exit(0); }
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
