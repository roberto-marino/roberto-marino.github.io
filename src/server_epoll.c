/*
 * server_epoll_demo.c
 *
 * Server multi-client con epoll() — equivalente a server_select_demo.c
 * ma usa epoll invece di select.
 *
 * Compilazione:
 *   gcc -Wall -O2 -o server_epoll_demo server_epoll_demo.c
 *
 * Esecuzione:
 *   ./server_epoll_demo
 *
 * Test con netcat:
 *   nc 127.0.0.1 8080
 *
 * Test con lo script:
 *   ./test_multifd.sh 5
 *   ./test_multifd.sh 10
 *
 * Cosa osservare rispetto a server_select_demo:
 *   - NON c'e' FD_ZERO/FD_SET ad ogni ciclo
 *   - NON c'e' max_fd da tenere aggiornato
 *   - epoll_wait() ritorna SOLO i fd con eventi
 *   - epoll_ctl() si chiama una volta sola per fd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>

#define PORT        8080
#define MAX_EVENTS  64
#define BUFLEN      1024

/*  Funzione helper: rende un fd non bloccante ------------------------
 * In modalita' level-triggered (default) non e' strettamente
 * obbligatorio, ma e' buona pratica con epoll.
 * --------------------------------------------------------------------*/
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) { perror("fcntl F_GETFL"); exit(1); }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL"); exit(1);
    }
}

int main(void)
{
    /* -- 1. Socket di ascolto ------------------------------------------*/
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(server_fd);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, 128) == -1) { perror("listen"); exit(1); }

    printf("=== Server epoll avviato sulla porta %d ===\n", PORT);
    printf("socket di ascolto: fd=%d\n\n", server_fd);

    /* -- 2. Crea l'istanza epoll ------------------------------------- */
    int epfd = epoll_create1(0);
    if (epfd == -1) { perror("epoll_create1"); exit(1); }

    /* -- 3. Registra server_fd - UNA VOLTA SOLA ----------------------*/
    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl ADD server_fd"); exit(1);
    }

    printf("In attesa di connessioni...\n");
    printf("(testa con: nc 127.0.0.1 %d  oppure  ./test_multifd.sh 5)\n\n",
           PORT);

    /* -- 4. Loop principale -------------------------------------------*/
    struct epoll_event events[MAX_EVENTS];

    while (1) {
        /*
         * epoll_wait() si blocca finche' almeno un fd e' pronto.
         * A differenza di select():
         *   - non passiamo nessun set da ricostruire
         *   - non calcoliamo nessun max_fd
         *   - ritorna SOLO gli fd con eventi attivi
         */
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); break;
        }

        printf(">>> epoll_wait() ritorna: %d fd pronti\n", n);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            /* -- Evento su server_fd: nuovo client -------------------*/
            if (fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t addrlen = sizeof(client_addr);

                int client_fd = accept(server_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addrlen);
                if (client_fd == -1) { perror("accept"); continue; }

                set_nonblocking(client_fd);

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr,
                          ip, sizeof(ip));

                printf("[+] nuovo client\n");
                printf("    socket di ascolto    : fd=%d (invariato)\n",
                       server_fd);
                printf("    socket di connessione: fd=%d  %s:%d\n",
                       client_fd, ip, ntohs(client_addr.sin_port));

                /*
                 * Registra il nuovo client in epoll — UNA VOLTA SOLA.
                 * Non serve aggiornare nessun array o max_fd.
                 */
                ev.events  = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl ADD client"); close(client_fd);
                }

            /* -- Evento su client_fd: dati o disconnessione ------------------ */
            } else if (events[i].events & EPOLLIN) {
                char buf[BUFLEN];
                ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);

                if (r <= 0) {
                    /*
                     * r==0: client ha chiuso la connessione
                     * r==-1: errore
                     *
                     * Rimuovi da epoll prima di chiudere il fd.
                     * (In kernel >= 2.6.9 il DEL e' opzionale
                     * se si chiude il fd, ma e' buona pratica.)
                     */
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    printf("[-] client disconnesso (fd=%d)\n", fd);

                } else {
                    buf[r] = '\0';
                    /* rimuovi newline finale (netcat aggiunge \n) */
                    if (r > 0 && buf[r-1] == '\n') buf[--r] = '\0';
                    printf("[fd=%d] messaggio: \"%s\" (%zd byte)\n",
                           fd, buf, r);
                }

            /*  Errore o hang-up sul fd ------------------------- */
            } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                printf("[!] errore/hangup su fd=%d, chiuso\n", fd);
            }
        }
    }

    close(epfd);
    close(server_fd);
    return 0;
}
