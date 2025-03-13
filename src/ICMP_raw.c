#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define PACKET_SIZE 64
#define TIMEOUT_SEC 2

// Calcola il checksum ICMP
unsigned short compute_checksum(void *addr, int len) {
    int sum = 0;
    unsigned short *ptr = addr;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(unsigned char *)ptr;
    sum = (sum >> 16) + (sum & 0xFFFF);
    return (unsigned short)(~sum);
}

// Crea un pacchetto ICMP di tipo ECHO REQUEST
void build_icmp_packet(struct icmphdr *icmp, int sequence) {
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = htons(getpid());
    icmp->un.echo.sequence = htons(sequence);
    icmp->checksum = 0;
    
    // Aggiunge un payload qualunque
    char *data = (char *)(icmp + 1);
    memset(data, 'A', PACKET_SIZE - sizeof(*icmp));
    
    icmp->checksum = compute_checksum(icmp, PACKET_SIZE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Utilizzo: %s <indirizzo IP>\n", argv[0]);
        exit(1);
    }

    int sock;
    struct sockaddr_in dest;
    char packet[PACKET_SIZE];
    struct icmphdr *icmp = (struct icmphdr *)packet;

    // Crea socket raw per ICMP
    if ((sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
        perror("socket");
        exit(1);
    }

    // Configura un timeout in ricezione
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Configura la destinazione
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(argv[1]);

    // Costruisce e invia il pacchetto
    build_icmp_packet(icmp, 1);
    printf("Invio ping a %s...\n", argv[1]);
    if (sendto(sock, packet, PACKET_SIZE, 0, (struct sockaddr *)&dest, sizeof(dest)) <= 0) {
        perror("sendto");
        close(sock);
        exit(1);
    }

    // Riceve la risposta
    char recv_buffer[1024];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    
    int bytes = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&src, &src_len);
    if (bytes < 0) {
        perror("Nessuna risposta");
        close(sock);
        exit(1);
    }

    // Analizza la risposta, si aspetta ECHO_REPLY
    struct iphdr *ip_hdr = (struct iphdr *)recv_buffer;
    struct icmphdr *icmp_reply = (struct icmphdr *)(recv_buffer + (ip_hdr->ihl * 4));
    
    if (icmp_reply->type == ICMP_ECHOREPLY && 
        ntohs(icmp_reply->un.echo.id) == getpid()) {
        printf("Ricevuta risposta da %s: seq=%d, TTL=%d\n",
               inet_ntoa(src.sin_addr),
               ntohs(icmp_reply->un.echo.sequence),
               ip_hdr->ttl);
    } else {
        printf("Risposta non valida\n");
    }

    close(sock);
    return 0;
}
