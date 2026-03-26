/*
 * sender.c — Invia un messaggio a un gruppo multicast UDP
 *
 * Uso: ./sender <messaggio>
 *
 * Compilazione:
 *   gcc sender.c -o sender -Wall -Wextra
 *
 * Reti e Sistemi Distribuiti Mod.B — Università di Messina
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MCAST_GROUP "239.0.0.1"
#define MCAST_PORT   5000

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <messaggio>\n", argv[0]);
        return 1;
    }

    /* 1. Crea un normale socket UDP.
          Il multicast si appoggia sempre su UDP — non su TCP.       */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    /* 2. IP_MULTICAST_TTL: quanti router puo' attraversare il pacchetto.
          TTL = 1 significa "resta nella LAN locale" (non viene instradato).
          E' il valore di default per il multicast, ma e' buona norma
          impostarlo esplicitamente per chiarezza.                    */
    int ttl = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt IP_MULTICAST_TTL");
        close(sockfd);
        return 1;
    }

    /* 3. Indirizzo di destinazione = indirizzo del gruppo multicast.
          Non c'e' bisogno di bind() per il sender: il kernel sceglie
          automaticamente la porta sorgente e l'interfaccia di uscita. */
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(MCAST_PORT);
    inet_pton(AF_INET, MCAST_GROUP, &dest.sin_addr);

    /* 4. sendto: invia il messaggio passato sulla riga di comando.
          Il kernel calcola automaticamente il MAC di destinazione
          (01:00:5e:00:00:01) dall'indirizzo IP del gruppo.           */
    const char *msg = argv[1];
    ssize_t sent = sendto(sockfd, msg, strlen(msg), 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }

    printf("Inviato (%zd byte): \"%s\"\n  -> gruppo %s porta %d\n",
           sent, msg, MCAST_GROUP, MCAST_PORT);

    close(sockfd);
    return 0;
}
