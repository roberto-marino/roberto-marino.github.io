#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int main() {
    int sockfd;
    char buffer[1024];
    struct sockaddr_in servaddr;

    // Creazione del socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Configurazione dell'indirizzo
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    int n, len;

    // Invio di un messaggio al server
    sendto(sockfd, (const char *)"Hello from client", strlen("Hello from client"), MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr));
    printf("Hello message sent.\n");

    // Ricezione della risposta dal server
    n = recvfrom(sockfd, (char *)buffer, 1024, MSG_WAITALL, (struct sockaddr *) &servaddr, &len);
    buffer[n] = '\0';
    printf("Server : %s\n", buffer);

    // Chiusura del socket
    close(sockfd);
    return 0;
}
