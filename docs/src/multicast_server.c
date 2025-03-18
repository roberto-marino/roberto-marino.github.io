#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MULTICAST_GROUP "239.0.0.1"
#define PORT 12345

int main() {
    int sockfd;
    struct sockaddr_in multicast_addr;
    char *message = "Hello, Multicast!";

    // Crea un socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setta l'indirizzo multicat
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(PORT);
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);

    // Spedisce i dati al gruppo multicast ogni 2 secondi
    while (1) {
        if (sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
            perror("Sendto failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        printf("Sent: %s\n", message);
        sleep(2); 
    }

    close(sockfd);
    return 0;
}
