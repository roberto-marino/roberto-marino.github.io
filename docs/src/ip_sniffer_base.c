#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>    /* socket(), recvfrom()                  */
#include <netinet/ip.h>    /* struct iphdr                          */
#include <netinet/in.h>    /* IPPROTO_ICMP, struct sockaddr_in      */
#include <arpa/inet.h>     /* inet_ntoa()                           */

#define BUFSIZE 65536

int main(void)
{
    /* AF_INET + SOCK_RAW + IPPROTO_ICMP:
       il kernel consegna tutti i pacchetti ICMP in arrivo,
       gia' decapsulati dal frame Ethernet.
       Il buffer inizia direttamente dall'header IP. */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) { perror("socket"); return 1; }

    printf("In ascolto (solo ICMP)... premi Ctrl+C per uscire\n\n");

    unsigned char buf[BUFSIZE];
    int pkt = 0;

    while (1) {
        ssize_t n = recvfrom(sock, buf, BUFSIZE, 0, NULL, NULL);
        if (n < 0) { perror("recvfrom"); break; }

        /* I primi byte del buffer sono l'header IP */
        struct iphdr *ip = (struct iphdr *)buf;

        /* tot_len e' in network byte order: serve ntohs() */
        printf("Pacchetto #%d  (%d byte)\n",
               ++pkt, ntohs(ip->tot_len));

        /* inet_ntoa() usa un buffer statico interno:
           chiamarla due volte nella stessa printf sovrascrive
           il primo risultato. Per ora usiamo due printf separate. */
        struct in_addr s, d;
        s.s_addr = ip->saddr;
        d.s_addr = ip->daddr;
        printf("  SRC: %s\n", inet_ntoa(s));
        printf("  DST: %s\n", inet_ntoa(d));

        printf("  TTL: %d  Proto: %d\n\n",
               ip->ttl, ip->protocol);
    }

    close(sock);
    return 0;
}
