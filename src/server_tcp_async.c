#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

int server_fd; // Socket del server
int client_fds[MAX_CLIENTS] = { -1 }; // Array per i client connessi
int num_clients = 0; // Contatore client attivi

// Handler per il segnale SIGIO
void handle_sigio(int sig) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // 1. Gestione nuove connessioni (sul socket del server)
    int new_client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (new_client >= 0) {
        if (num_clients < MAX_CLIENTS) {
            // Configura il socket del client come non-bloccante e O_ASYNC
            fcntl(new_client, F_SETFL, O_NONBLOCK | O_ASYNC);
            fcntl(new_client, F_SETOWN, getpid());

            client_fds[num_clients++] = new_client;
            write(STDOUT_FILENO, "Client connesso\n", 16);
        } else {
            close(new_client);
            write(STDOUT_FILENO, "Limite client raggiunto\n", 24);
        }
    } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
        perror("accept");
    }

    // 2. Gestione dati dai client esistenti
    for (int i = 0; i < num_clients; i++) {
        int client = client_fds[i];
        if (client == -1) continue;

        ssize_t bytes = recv(client, buffer, BUFFER_SIZE, 0);
        if (bytes > 0) {
            // Echo dei dati ricevuti
            send(client, buffer, bytes, 0);
            write(STDOUT_FILENO, "Dati inviati al client\n", 22);
        } else if (bytes == 0 || (bytes == -1 && (errno != EWOULDBLOCK && errno != EAGAIN))) {
            // Client disconnesso o errore
            close(client);
            client_fds[i] = -1; // Marca il client come chiuso
            write(STDOUT_FILENO, "Client disconnesso\n", 19);
        }
    }
}

int main() {
    struct sockaddr_in addr;
    struct sigaction sa;

    // Creazione socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configura indirizzo del server
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Configura il socket del server come non-bloccante e O_ASYNC
    fcntl(server_fd, F_SETFL, O_NONBLOCK | O_ASYNC);
    fcntl(server_fd, F_SETOWN, getpid()); // Il processo riceverà SIGIO

    // Configura l'handler per SIGIO
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigio;
    sa.sa_flags = SA_RESTART; // Riabilita syscall interrotte
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGIO, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("Server in ascolto su porta %d (CTRL+C per uscire)...\n", PORT);

    // Loop principale: resta in attesa di segnali
    while (1) {
        pause(); // Sospende il processo finché non arriva un segnale
    }

    close(server_fd);
    return 0;
}
