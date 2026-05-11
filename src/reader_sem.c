/* reader.c
 * Esercizio 1 -- Semafori POSIX con nome: reader.
 * Avviare PRIMA di writer.
 *
 * Compilazione:
 *   gcc -Wall -Wextra -o reader reader.c -lrt
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>

#define SEM_READY "/sem_ready"
#define SEM_DONE  "/sem_done"
#define DATAFILE  "/tmp/dati.bin"
#define N         100

static sem_t *s_ready, *s_done;

static void cleanup(int sig) {
    (void)sig;
    if (s_ready) { sem_close(s_ready); sem_unlink(SEM_READY); }
    if (s_done)  { sem_close(s_done);  sem_unlink(SEM_DONE);  }
    unlink(DATAFILE);
    _exit(0);
}

int main(void) {
    signal(SIGINT, cleanup);

    /* TODO: crea entrambi i semafori.
     *   s_ready = sem_open(SEM_READY, O_CREAT|O_EXCL, 0666, 0);
     *   s_done  = sem_open(SEM_DONE,  O_CREAT|O_EXCL, 0666, 0); */

    printf("[reader] pronto, segnalo il writer\n");
    /* TODO: sem_post(s_ready) */

    printf("[reader] attendo che il writer finisca...\n");
    /* TODO: sem_wait(s_done) */

    printf("[reader] leggo i dati e cerco il massimo\n");
    /* TODO: apri DATAFILE in lettura, leggi N interi,
     *       trova il massimo e stampalo.               */

    /* pulizia finale */
    sem_close(s_ready); sem_unlink(SEM_READY);
    sem_close(s_done);  sem_unlink(SEM_DONE);
    unlink(DATAFILE);
    return 0;
}
