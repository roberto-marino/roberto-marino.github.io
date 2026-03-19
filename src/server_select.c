/*
 * server_select_demo.c
 *
 * Dimostra la differenza tra:
 *   - socket di ascolto  (server_fd): uno solo, riceve connessioni
 *   - socket di connessione (client_fd): uno per client, scambia dati
 *
 * Compilazione:
 *   gcc -Wall -O2 -o server_select_demo server_select_demo.c
 *
 * Esecuzione:
 *   ./server_select_demo
 *
 * Test con netcat (in un altro terminale):
 *   nc 127.0.0.1 8080
 *   (scrivi messaggi e premi invio — arrivano al server)
 *   (apri piu' terminali con nc per testare piu' client)
 *
 * Uscita dal server: CTRL+C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define PORT        8080
#define MAX_CLIENTS 10
#define BUFLEN      1024

int main(void)
{
    /* ── Socket di ascolto ────────────────────────────────────────
     * server_fd e' il socket di ASCOLTO.
     * Serve solo a ricevere richieste di connessione dai client.
     * Non scambia mai dati applicativi.
     * Ne esiste UNO SOLO per tutto il ciclo di vita del server.
     * ─────────────────────────────────────────────────────────── */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); exit(1); }

    /* Riutilizza la porta subito dopo la chiusura */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); exit(1);
    }

    /* listen() attiva il socket di ascolto:
     * da questo momento il kernel accoda le richieste di connessione */
    if (listen(server_fd, 5) == -1) { perror("listen"); exit(1); }

    printf("=== Server avviato sulla porta %d ===\n", PORT);
    printf("socket di ascolto: fd=%d\n", server_fd);
    printf("In attesa di connessioni... (testa con: nc 127.0.0.1 %d)\n\n",
           PORT);

    /* ── Strutture dati per select() ─────────────────────────────
     * client_fds[]: rubrica dei socket di connessione attivi.
     *               -1 = slot libero.
     * max_fd:       fd piu' grande tra tutti quelli monitorati.
     *               All'inizio e' server_fd (unico fd presente).
     * ─────────────────────────────────────────────────────────── */
    int client_fds[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++)
        client_fds[i] = -1;

    int max_fd = server_fd; /* unico fd monitorato all'avvio */

    /* ── Loop principale ─────────────────────────────────────────*/
    while (1) {

        /* Ricostruisci il set ad ogni ciclo:
         * select() lo sovrascrive, quindi va azzerato */
        fd_set read_set;
        FD_ZERO(&read_set);

        /* Aggiungi sempre il socket di ascolto:
         * vogliamo sapere quando arriva un nuovo client */
        FD_SET(server_fd, &read_set);

        /* Aggiungi tutti i socket di connessione attivi */
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (client_fds[i] != -1)
                FD_SET(client_fds[i], &read_set);

        /* Blocca finche' almeno un fd e' pronto */
        int n = select(max_fd + 1, &read_set, NULL, NULL, NULL);
        if (n == -1) {
            if (errno == EINTR) continue; /* segnale: riprova */
            perror("select"); break;
        }

        /* ── Evento su server_fd: nuovo client ───────────────────
         * accept() crea un NUOVO socket di connessione dedicato
         * a questo client. server_fd resta invariato e continua
         * ad ascoltare le prossime connessioni.
         * ─────────────────────────────────────────────────────── */
        if (FD_ISSET(server_fd, &read_set)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);

            int client_fd = accept(server_fd,
                                   (struct sockaddr *)&client_addr,
                                   &addrlen);
            if (client_fd == -1) { perror("accept"); continue; }

            /* Stampa le informazioni sul nuovo socket di connessione */
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
            printf("[+] nuovo client connesso\n");
            printf("    socket di ascolto  : fd=%d (invariato)\n",
                   server_fd);
            printf("    socket di connessione: fd=%d  %s:%d\n",
                   client_fd, ip, ntohs(client_addr.sin_port));

            /* Salva il nuovo fd nella rubrica */
            int saved = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] == -1) {
                    client_fds[i] = client_fd;
                    saved = 1;
                    break;
                }
            }
            if (!saved) {
                printf("    [!] troppi client, rifiuto fd=%d\n",
                       client_fd);
                close(client_fd);
                continue;
            }

            /* Aggiorna max_fd se necessario */
            if (client_fd > max_fd)
                max_fd = client_fd;
        }

        /* ── Evento su un client_fd: dati o disconnessione ───────
         * Scorri tutti i socket di connessione e servi quelli pronti.
         * ─────────────────────────────────────────────────────── */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1)
                continue; /* slot vuoto */

            if (!FD_ISSET(client_fds[i], &read_set))
                continue; /* non pronto in questo ciclo */

            char buf[BUFLEN];
            ssize_t r = recv(client_fds[i], buf, sizeof(buf) - 1, 0);

            if (r <= 0) {
                /* r==0: client ha chiuso la connessione
                   r==-1: errore                          */
                printf("[-] client disconnesso (fd=%d)\n",
                       client_fds[i]);
                close(client_fds[i]);
                client_fds[i] = -1; /* libera slot */

                /* Ricalcola max_fd */
                max_fd = server_fd;
                for (int j = 0; j < MAX_CLIENTS; j++)
                    if (client_fds[j] > max_fd)
                        max_fd = client_fds[j];

            } else {
                buf[r] = '\0';
                /* Rimuovi newline finale (netcat aggiunge \n) */
                if (r > 0 && buf[r-1] == '\n') buf[--r] = '\0';
                printf("[fd=%d] messaggio: \"%s\" (%zd byte)\n",
                       client_fds[i], buf, r);
            }
        }
    }

    close(server_fd);
    return 0;
}
