// Per generare traffico ARP installare il tool arping ed arp-pingare un host di rete (su rete fisica): arping -I eth0 192.168.1.1, oppure provare ad arp-pingare l'AP
// Per generare traffico pingate il dns di google 8.8.8.8
// Per generare traffico IPV6 pingate localhost con ping6 ::1


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>       /* socket(), bind(), AF_PACKET       */
#include <netpacket/packet.h> /* struct sockaddr_ll                */
#include <net/ethernet.h>     /* struct ethhdr, ETH_P_ALL          */
#include <arpa/inet.h>        /* htons(), ntohs()                  */
#include <net/if.h>           /* if_nametoindex()                  */

#define BUFSIZE 65536

int main(int argc, char *argv[])
{
    /* PROBLEMA 1: nessun controllo sugli argomenti */
    const char *iface = argv[1];

    /* Apre il raw socket: cattura tutti i protocolli */
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    /* PROBLEMA 2: valore di ritorno di socket() non controllato */

    /* Recupera l'indice numerico dell'interfaccia */
    unsigned int ifindex = if_nametoindex(iface);

    /* Associa il socket all'interfaccia specificata */
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex  = ifindex;
    bind(sock, (struct sockaddr *)&sll, sizeof(sll));
    /* PROBLEMA 3: valore di ritorno di bind() non controllato */

    unsigned char buf[BUFSIZE];
    int frame = 0;

    while (1) {
        ssize_t n = recvfrom(sock, buf, BUFSIZE, 0, NULL, NULL);
        if (n < (ssize_t)sizeof(struct ethhdr)) continue;

        struct ethhdr *eth = (struct ethhdr *)buf;

        printf("Frame #%d  (%zd byte)\n", ++frame, n);

        /* Stampa i MAC address: ogni byte in esadecimale */
        printf("  DST: %02X:%02X:%02X:%02X:%02X:%02X\n",
            eth->h_dest[0],   eth->h_dest[1],   eth->h_dest[2],
            eth->h_dest[3],   eth->h_dest[4],   eth->h_dest[5]);
        printf("  SRC: %02X:%02X:%02X:%02X:%02X:%02X\n",
            eth->h_source[0], eth->h_source[1], eth->h_source[2],
            eth->h_source[3], eth->h_source[4], eth->h_source[5]);

        /* PROBLEMA 4: EtherType stampato senza conversione byte order
           e senza decodifica del protocollo */
        printf("  EtherType: 0x%04X\n", eth->h_proto);
    }

    close(sock);
    return 0;
}
