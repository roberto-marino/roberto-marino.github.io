#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 256

int main() {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    // Crea il socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Configura l'indirizzo del server
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("indirizzo non valido");
        exit(EXIT_FAILURE);
    }

    // Connette al server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connessione fallita");
        exit(EXIT_FAILURE);
    }

    printf("Connesso al server. Invia messaggi:\n");

    while (1) {
        printf("> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send(sockfd, buffer, strlen(buffer), 0);

        if (strcmp(buffer, "exit\n") == 0) {
            printf("Uscita...\n");
            break;
        }

        int n = read(sockfd, buffer, BUFFER_SIZE - 1);
        buffer[n] = '\0';
        printf("ACK del server: %s", buffer);
    }

    close(sockfd);
    return 0;
}
