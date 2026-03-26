/*
 * receiver.c — Riceve messaggi da un gruppo multicast UDP
 *
 * Uso: ./receiver
 *      (termina con Ctrl+C)
 *
 * Compilazione:
 *   gcc receiver.c -o receiver -Wall -Wextra
 *
 * Reti e Sistemi Distribuiti Mod.B — Università di Messina
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>

#define MCAST_GROUP "239.0.0.1"
#define MCAST_PORT   5000
#define BUFLEN       1024

/* Variabile globale per il cleanup sul segnale SIGINT */
static int sockfd_global = -1;
static struct ip_mreq mreq_global;

static void sigint_handler(int sig)
{
    (void)sig;
    printf("\nUscita dal gruppo e chiusura...\n");

    /* IP_DROP_MEMBERSHIP: il kernel manda un IGMP Leave al router */
    setsockopt(sockfd_global, IPPROTO_IP, IP_DROP_MEMBERSHIP,
               &mreq_global, sizeof(mreq_global));
    close(sockfd_global);
    exit(0);
}

int main(void)
{
    /* 1. Crea socket UDP */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    sockfd_global = sockfd;

    /* 2. SO_REUSEADDR: permette a piu' processi di fare bind sulla
          stessa porta. Indispensabile se vuoi avviare piu' receiver
          sulla stessa macchina durante i test.                      */
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                   &yes, sizeof(yes)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(sockfd);
        return 1;
    }

    /* 3. bind() sull'indirizzo del gruppo e sulla porta.
          Usare l'indirizzo del gruppo (239.0.0.1) invece di
          INADDR_ANY e' piu' preciso: il kernel filtra qui i
          datagrammi destinati ad altri gruppi sulla stessa porta.   */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port   = htons(MCAST_PORT);
    inet_pton(AF_INET, MCAST_GROUP, &local.sin_addr);

    if (bind(sockfd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    /* 4. IP_ADD_MEMBERSHIP: iscrizione al gruppo multicast.
          Questa chiamata fa due cose:
            a) Dice al kernel di accettare i pacchetti per questo gruppo
            b) Il kernel invia un messaggio IGMP Join al router locale
          imr_interface = INADDR_ANY: usa l'interfaccia di default.  */
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, MCAST_GROUP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    mreq_global = mreq;

    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP");
        close(sockfd);
        return 1;
    }

    /* Installa il gestore per Ctrl+C: uscita pulita dal gruppo */
    signal(SIGINT, sigint_handler);

    printf("Iscritto al gruppo %s porta %d\n", MCAST_GROUP, MCAST_PORT);
    printf("In ascolto... (Ctrl+C per uscire)\n\n");

    /* 5. Loop di ricezione */
    char buf[BUFLEN];
    struct sockaddr_in sender;
    socklen_t senderlen = sizeof(sender);

    while (1) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &senderlen);
        if (n < 0) {
            perror("recvfrom");
            break;
        }
        buf[n] = '\0';

        /* inet_ntop e' thread-safe; inet_ntoa NON lo e'
           (usa un buffer statico interno che verrebbe sovrascritto) */
        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));

        printf("[%s:%d] %s\n",
               sender_ip,
               ntohs(sender.sin_port),
               buf);
    }

    /* Cleanup (raggiunto solo in caso di errore su recvfrom) */
    setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
               &mreq, sizeof(mreq));
    close(sockfd);
    return 0;
}
