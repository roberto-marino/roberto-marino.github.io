/* raft_impl.c — implementazione didattica del consenso Raft
 *
 * Topologia: N processi su localhost, UDP, porte BASE_PORT..BASE_PORT+N-1.
 * Il processo padre (main) funge da client interattivo.
 *
 * Compilazione:  gcc -Wall -Wextra -o raft raft_impl.c
 * Esecuzione:    ./raft
 *                Digita interi + Invio per inviare comandi al cluster.
 *                Ctrl-C per uscire.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* -- Costanti ------------------------------------------------------ */
#define N         5      /* numero di server nel cluster              */
#define BASE_PORT 5000   /* server i ascolta su BASE_PORT+i           */
#define MAX_LOG   256    /* lunghezza massima del log (entry)         */
#define HB_MS     50     /* heartbeat ogni HB_MS millisecondi         */
#define EMIN_MS   150    /* timeout elezione: minimo in ms            */
#define EMAX_MS   300    /* timeout elezione: massimo in ms (random)  */
#define AE_BATCH  8      /* entry max per messaggio AppendEntries     */

/* -- Tipi di messaggio --------------------------------------------- */
#define MSG_RV      1   /* RequestVote          candidate → tutti    */
#define MSG_RVR     2   /* RequestVote Reply    tutti     → candidate */
#define MSG_AE      3   /* AppendEntries        leader   → follower  */
#define MSG_AER     4   /* AppendEntries Reply  follower → leader    */
#define MSG_CLIENT  5   /* Comando client       client   → tutti     */

/* -- Stato del server --------------------------------------------- */
typedef enum { FOLLOWER, CANDIDATE, LEADER } role_t;
static const char *ROLE[] = { "FOLLOWER", "CANDIDATE", "LEADER" };

/* Un'entry del log: il term in cui è stata creata + il comando */
typedef struct { int term; int cmd; } entry_t;

/* Header comune a tutti i messaggi */
typedef struct { int type; int term; int from; } hdr_t;

/* -- Strutture dei messaggi ---------------------------------------- */

/* RequestVote: il candidato dichiara il proprio log */
typedef struct {
    hdr_t h;
    int   last_log_idx;   /* indice ultima entry nel log del candidato */
    int   last_log_term;  /* term   ultima entry nel log del candidato */
} msg_rv_t;

/* RequestVote Reply */
typedef struct {
    hdr_t h;
    int   granted;        /* 1 = voto concesso, 0 = negato            */
} msg_rvr_t;

/* AppendEntries (anche heartbeat se n==0) */
typedef struct {
    hdr_t   h;
    int     prev_idx;          /* indice entry immediatamente precedente */
    int     prev_term;         /* term  entry immediatamente precedente  */
    int     commit;            /* commitIndex del leader                 */
    int     n;                 /* numero di entry nel messaggio          */
    entry_t entries[AE_BATCH];
} msg_ae_t;

/* AppendEntries Reply */
typedef struct {
    hdr_t h;
    int   success;     /* 1 = log consistente, entry aggiunte          */
    int   match;       /* massimo indice replicato con successo         */
} msg_aer_t;

/* Comando client */
typedef struct {
    hdr_t h;
    int   cmd;
} msg_client_t;

/* Union: permette di leggere qualsiasi messaggio in un buffer unico */
typedef union {
    hdr_t        h;
    msg_rv_t     rv;
    msg_rvr_t    rvr;
    msg_ae_t     ae;
    msg_aer_t    aer;
    msg_client_t client;
} msg_t;

/* -- Stato completo di un server ----------------------------------- */
typedef struct {
    /* identità */
    int    id;
    role_t role;

    /* stato persistente (in un sistema reale: su disco) */
    int     term;         /* currentTerm: il term più alto visto       */
    int     voted_for;    /* -1 = non votato in questo term            */
    entry_t log[MAX_LOG];
    int     log_len;      /* numero di entry nel log (indici 1-based)  */

    /* stato volatile */
    int  commit_idx;      /* indice ultima entry committed             */
    int  votes;           /* voti raccolti come candidate              */

    /* stato volatile del leader (si azzera a ogni elezione) */
    int  next_idx[N];     /* prossima entry da inviare a ogni follower */
    int  match_idx[N];    /* ultima entry confermata da ogni follower  */

    /* timing */
    long hb_recv_at;      /* timestamp ultimo heartbeat ricevuto (ms)  */
    long hb_sent_at;      /* timestamp ultimo heartbeat inviato  (ms)  */
    int  elect_ms;        /* timeout elezione (scelto casualmente)     */

    /* socket UDP */
    int  fd;
} srv_t;

/* ═══════════════════════════════════════════════════════════════════
 * Utilità
 * ═══════════════════════════════════════════════════════════════════ */

/* Millisecondi monotoni dall'avvio del sistema */
static long ms_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* Invia buf su UDP alla porta indicata (localhost) */
static void udp_send(int fd, int port, const void *buf, size_t len) {
    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a));
}

static void send_to(srv_t *s, int dest, const void *m, size_t len) {
    udp_send(s->fd, BASE_PORT + dest, m, len);
}

static void broadcast(srv_t *s, const void *m, size_t len) {
    for (int i = 0; i < N; i++)
        if (i != s->id)
            send_to(s, i, m, len);
}

/* Restituisce il term dell'entry in posizione idx (1-based); 0 se fuori range */
static int log_term_at(srv_t *s, int idx) {
    return (idx > 0 && idx <= s->log_len) ? s->log[idx-1].term : 0;
}

/* §5.4.1: il log "loro" è almeno aggiornato quanto "il mio"?
 * Prima si confronta il term dell'ultima entry, poi la lunghezza. */
static int at_least_uptodate(int my_term, int my_idx,
                               int their_term, int their_idx) {
    if (their_term != my_term) return their_term > my_term;
    return their_idx >= my_idx;
}

/* Sceglie un nuovo timeout di elezione casuale e azzera il timer */
static void reset_election_timer(srv_t *s) {
    s->hb_recv_at = ms_now();
    s->elect_ms   = EMIN_MS + rand() % (EMAX_MS - EMIN_MS);
}

/* ═══════════════════════════════════════════════════════════════════
 * Transizioni di stato
 * ═══════════════════════════════════════════════════════════════════ */

static void become_follower(srv_t *s, int new_term) {
    if (s->role != FOLLOWER)
        printf("[S%d] %s → FOLLOWER  (term %d)\n",
               s->id, ROLE[s->role], new_term);
    else if (s->term != new_term)
        printf("[S%d] term aggiornato: %d → %d\n",
               s->id, s->term, new_term);
    s->role      = FOLLOWER;
    s->term      = new_term;
    s->voted_for = -1;
    s->votes     = 0;
    reset_election_timer(s);
}

/* Scaduto il timeout di elezione: incrementa il term e chiedi voti */
static void start_election(srv_t *s) {
    s->term++;
    s->role      = CANDIDATE;
    s->voted_for = s->id;  /* vota per sé stesso */
    s->votes     = 1;
    reset_election_timer(s);

    printf("[S%d] FOLLOWER → CANDIDATE  (term %d)\n", s->id, s->term);

    msg_rv_t m = {
        .h             = { MSG_RV, s->term, s->id },
        .last_log_idx  = s->log_len,
        .last_log_term = log_term_at(s, s->log_len),
    };
    broadcast(s, &m, sizeof(m));
}

/* Voti sufficienti raccolti: diventa leader */
static void become_leader(srv_t *s) {
    printf("[S%d] CANDIDATE → LEADER  (term %d)\n", s->id, s->term);
    s->role = LEADER;

    /* inizializza lo stato del leader per ogni follower */
    for (int i = 0; i < N; i++) {
        s->next_idx[i]  = s->log_len + 1; /* ottimismo: il follower è aggiornato */
        s->match_idx[i] = 0;
    }
    s->hb_sent_at = 0; /* forza heartbeat immediato */
}

/* ═══════════════════════════════════════════════════════════════════
 * Replicazione del log
 * ═══════════════════════════════════════════════════════════════════ */

/* Invia AppendEntries (o heartbeat se il follower è aggiornato) */
static void send_ae(srv_t *s, int dest) {
    msg_ae_t m;
    m.h      = (hdr_t){ MSG_AE, s->term, s->id };
    m.commit = s->commit_idx;

    /* l'entry precedente serve al follower per verificare la consistenza */
    int prev    = s->next_idx[dest] - 1;
    m.prev_idx  = prev;
    m.prev_term = log_term_at(s, prev);

    /* copia le entry da inviare (da next_idx[dest] in poi) */
    int n = 0;
    for (int i = s->next_idx[dest];
         i <= s->log_len && n < AE_BATCH; i++, n++)
        m.entries[n] = s->log[i-1];
    m.n = n;

    send_to(s, dest, &m, sizeof(m));
}

/* Invia heartbeat a tutti i follower */
static void send_heartbeats(srv_t *s) {
    for (int i = 0; i < N; i++)
        if (i != s->id)
            send_ae(s, i);
    s->hb_sent_at = ms_now();
}

/* Dopo aver ricevuto una risposta positiva: aggiorna commitIndex.
 * §5.3: il leader fa commit di N se N è replicato sulla maggioranza
 *        E log[N].term == currentTerm. */
static void update_commit(srv_t *s) {
    for (int n = s->log_len; n > s->commit_idx; n--) {
        /* solo entry del term corrente possono essere committed direttamente */
        if (s->log[n-1].term != s->term) continue;
        int cnt = 1; /* il leader stesso ha l'entry */
        for (int i = 0; i < N; i++)
            if (i != s->id && s->match_idx[i] >= n) cnt++;
        if (cnt > N / 2) {
            s->commit_idx = n;
            printf("[S%d] LEADER: committed index %d  (cmd=%d)\n",
                   s->id, n, s->log[n-1].cmd);
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Handler dei messaggi in ingresso
 * ═══════════════════════════════════════════════════════════════════ */

static void on_request_vote(srv_t *s, msg_rv_t *m) {
    /* concedi il voto se:
     * 1. il term del candidato è >= al nostro
     * 2. non abbiamo già votato per qualcun altro in questo term
     * 3. il log del candidato è almeno aggiornato quanto il nostro */
    int grant = (m->h.term >= s->term) &&
                (s->voted_for == -1 || s->voted_for == m->h.from) &&
                at_least_uptodate(log_term_at(s, s->log_len), s->log_len,
                                   m->last_log_term, m->last_log_idx);
    if (grant) {
        s->voted_for = m->h.from;
        reset_election_timer(s); /* segnale di attività: non candidarci subito */
    }

    printf("[S%d] RequestVote da S%d (term %d): %s\n",
           s->id, m->h.from, m->h.term, grant ? "GRANTED" : "DENIED");

    msg_rvr_t r = { .h = { MSG_RVR, s->term, s->id }, .granted = grant };
    send_to(s, m->h.from, &r, sizeof(r));
}

static void on_vote_response(srv_t *s, msg_rvr_t *m) {
    /* ignora risposte obsolete o fuori ruolo */
    if (s->role != CANDIDATE || m->h.term != s->term) return;
    if (!m->granted) return;

    s->votes++;
    printf("[S%d] voto da S%d — totale %d/%d\n",
           s->id, m->h.from, s->votes, N);

    /* maggioranza raggiunta: diventa leader */
    if (s->votes > N / 2)
        become_leader(s);
}

static void on_append_entries(srv_t *s, msg_ae_t *m) {
    msg_aer_t r = { .h = { MSG_AER, s->term, s->id },
                    .success = 0, .match = 0 };

    /* messaggio da leader obsoleto: rifiuta e comunica il term corrente */
    if (m->h.term < s->term) {
        send_to(s, m->h.from, &r, sizeof(r));
        return;
    }

    /* leader valido: se eravamo candidate, torniamo follower */
    if (s->role == CANDIDATE)
        become_follower(s, m->h.term);

    /* aggiorna il timer — il leader è vivo */
    reset_election_timer(s);

    /* §5.3: controlla che il log sia consistente fino a prev_idx.
     * Se prev_idx non esiste o ha term sbagliato, il follower è indietro. */
    if (m->prev_idx > 0 &&
        (m->prev_idx > s->log_len ||
         log_term_at(s, m->prev_idx) != m->prev_term)) {
        send_to(s, m->h.from, &r, sizeof(r));
        return;
    }

    /* scrivi le nuove entry nel log */
    for (int i = 0; i < m->n; i++) {
        int idx = m->prev_idx + 1 + i;  /* 1-based */
        /* se c'è un conflitto (term diverso), tronca il log da qui */
        if (idx <= s->log_len && s->log[idx-1].term != m->entries[i].term)
            s->log_len = idx - 1;
        /* aggiungi l'entry se non è già presente */
        if (idx > s->log_len) {
            s->log[s->log_len] = m->entries[i];
            s->log_len++;
        }
    }

    /* avanza commitIndex se il leader è avanti */
    if (m->commit > s->commit_idx) {
        s->commit_idx = (m->commit < s->log_len) ? m->commit : s->log_len;
        printf("[S%d] follower: commitIndex aggiornato a %d\n",
               s->id, s->commit_idx);
    }

    r.success = 1;
    r.match   = m->prev_idx + m->n; /* fino a qui il log è allineato */
    send_to(s, m->h.from, &r, sizeof(r));
}

static void on_append_response(srv_t *s, msg_aer_t *m) {
    if (s->role != LEADER || m->h.term != s->term) return;

    int from = m->h.from;
    if (m->success) {
        /* il follower ha aggiunto le entry: aggiorna next/match */
        if (m->match > s->match_idx[from]) {
            s->match_idx[from] = m->match;
            s->next_idx[from]  = m->match + 1;
        }
        update_commit(s);
    } else {
        /* il follower non aveva l'entry prev: arretra e riprova */
        if (s->next_idx[from] > 1)
            s->next_idx[from]--;
        send_ae(s, from);
    }
}

static void on_client_cmd(srv_t *s, msg_client_t *m) {
    /* solo il leader elabora i comandi; gli altri li ignorano */
    if (s->role != LEADER) return;

    /* aggiunge l'entry al log locale */
    s->log[s->log_len++] = (entry_t){ .term = s->term, .cmd = m->cmd };
    printf("[S%d] LEADER: aggiunto index %d  (cmd=%d, term=%d)\n",
           s->id, s->log_len, m->cmd, s->term);

    /* avvia subito la replicazione */
    for (int i = 0; i < N; i++)
        if (i != s->id)
            send_ae(s, i);
}

/* ═══════════════════════════════════════════════════════════════════
 * Loop principale del server
 * ═══════════════════════════════════════════════════════════════════ */

static void server_loop(int id) {
    setbuf(stdout, NULL); /* disabilita buffering: printf immediato */
    /* seed diverso per ogni processo: timeout di elezione casuali */
    srand((unsigned)(time(NULL) ^ (getpid() * 2654435761U)));

    srv_t s;
    memset(&s, 0, sizeof(s));
    s.id         = id;
    s.role       = FOLLOWER;
    s.term       = 0;
    s.voted_for  = -1;

    /* crea e lega il socket UDP */
    s.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s.fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(BASE_PORT + id),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (bind(s.fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("bind"); exit(1);
    }
    reset_election_timer(&s);
    printf("[S%d] avviato sulla porta %d\n", id, BASE_PORT + id);

    for (;;) {
        long now = ms_now();
        long wait;

        /* calcola quanto aspettare prima del prossimo evento temporale */
        if (s.role == LEADER)
            wait = HB_MS - (now - s.hb_sent_at);
        else
            wait = s.elect_ms - (now - s.hb_recv_at);
        if (wait < 1) wait = 1;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s.fd, &fds);
        struct timeval tv = { 0, wait * 1000L };

        int ready = select(s.fd + 1, &fds, NULL, NULL, &tv);

        if (ready > 0) {
            /* messaggio in arrivo */
            msg_t m;
            recvfrom(s.fd, &m, sizeof(m), 0, NULL, NULL);

            /* §5.1: qualsiasi messaggio con term > currentTerm
             * ci riporta follower immediatamente */
            if (m.h.term > s.term)
                become_follower(&s, m.h.term);

            switch (m.h.type) {
                case MSG_RV:     on_request_vote(&s,    &m.rv);     break;
                case MSG_RVR:    on_vote_response(&s,   &m.rvr);    break;
                case MSG_AE:     on_append_entries(&s,  &m.ae);     break;
                case MSG_AER:    on_append_response(&s, &m.aer);    break;
                case MSG_CLIENT: on_client_cmd(&s,      &m.client); break;
            }
        } else {
            /* timeout: nessun messaggio ricevuto entro il tempo atteso */
            now = ms_now();
            if (s.role == LEADER) {
                if (now - s.hb_sent_at >= HB_MS)
                    send_heartbeats(&s);
            } else {
                /* nessun heartbeat ricevuto: il leader è morto, candidati */
                if (now - s.hb_recv_at >= s.elect_ms)
                    start_election(&s);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Client interattivo (processo padre)
 * ═══════════════════════════════════════════════════════════════════ */

static void client_loop(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket(client)"); return; }

    printf("\n[client] pronto.\n");
    printf("[client] Digita un intero + Invio per inviare un comando.\n");
    printf("[client] Il leader lo aggiungerà al log e lo replicherà.\n\n");

    int cmd;
    while (scanf("%d", &cmd) == 1) {
        /* invia a tutti: solo il leader lo processerà */
        msg_client_t m = {
            .h   = { MSG_CLIENT, 0, N },
            .cmd = cmd,
        };
        for (int i = 0; i < N; i++)
            udp_send(fd, BASE_PORT + i, &m, sizeof(m));
        printf("[client] comando %d inviato\n", cmd);
    }
    close(fd);
}

/* ═══════════════════════════════════════════════════════════════════
 * main: avvia N server e poi diventa il client
 * ═══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("=== Raft demo: %d server, porte %d-%d ===\n\n",
           N, BASE_PORT, BASE_PORT + N - 1);

    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) {
            server_loop(i);  /* figlio: loop infinito */
            exit(0);
        }
    }

    /* il padre diventa il client interattivo */
    sleep(1); /* lascia tempo ai server di avviarsi e eleggere un leader */
    client_loop();

    /* alla fine (Ctrl-C o EOF): termina tutti i figli */
    for (int i = 0; i < N; i++) wait(NULL);
    return 0;
}
