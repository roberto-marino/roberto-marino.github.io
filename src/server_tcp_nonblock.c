#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 256

void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

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

    set_nonblocking(sockfd); // Socket non bloccante
    printf("Server con attesa attiva in ascolto su porta %d...\n", PORT);

    while (1) {
        // Accetta connessioni in modalità non bloccante
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Nessuna connessione in arrivo: continua il loop
                continue;
            } else {
                perror("accept failed");
                exit(EXIT_FAILURE);
            }
        }

        printf("Nuovo client connesso\n");
        set_nonblocking(newsockfd); // Client socket non bloccante

        // Legge i dati in un loop con attesa attiva
        while (1) {
            int n = recv(newsockfd, buffer, BUFFER_SIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                printf("Ricevuto: %s", buffer);
                send(newsockfd, "ACK\n", 4, 0);
            } else if (n == 0) {
                printf("Client disconnesso\n");
                break;
            } else {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // Nessun dato disponibile: continua il loop
                    continue;
                } else {
                    perror("recv failed");
                    break;
                }
            }
        }
        close(newsockfd);
    }

    close(sockfd);
    return 0;
}
