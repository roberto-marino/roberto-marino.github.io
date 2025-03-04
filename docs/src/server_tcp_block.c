#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 256

int main() {
    int sockfd, newsockfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    char buffer[BUFFER_SIZE];

    // Crea il socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Configura l'indirizzo del server
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    // Collega il socket alla porta
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Mette il socket in ascolto
    if (listen(sockfd, 5) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server senza attesa attiva in ascolto su porta %d...\n", PORT);

    while (1) {
        // Accetta una connessione (bloccante)
        if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0) {
            perror("accept failed");
            continue;
        }

        printf("Nuovo client connesso\n");

        // Legge i dati in modo bloccante
        while (1) {
            int n = read(newsockfd, buffer, BUFFER_SIZE - 1);
            if (n <= 0) {
                printf("Client disconnesso\n");
                break;
            }
            buffer[n] = '\0';
            printf("Ricevuto: %s", buffer);
            write(newsockfd, "OK\n", 4);
        }

        close(newsockfd);
    }

    close(sockfd);
    return 0;
}
