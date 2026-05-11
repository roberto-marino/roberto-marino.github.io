/* writer.c
 * Esercizio 1 -- Semafori POSIX con nome: writer.
 * Avviare DOPO reader.
 *
 * Compilazione:
 *   gcc -Wall -Wextra -o writer writer.c -lrt
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define SEM_READY "/sem_ready"
#define SEM_DONE  "/sem_done"
#define DATAFILE  "/tmp/dati.bin"
#define N         100

static sem_t *s_ready, *s_done;

static void cleanup(int sig) {
    (void)sig;
    if (s_ready) sem_close(s_ready);
    if (s_done)  sem_close(s_done);
    /* il writer NON fa unlink: lo fa il reader che li ha creati */
    _exit(0);
}

int main(void) {
    signal(SIGINT, cleanup);

    /* TODO: apri i semafori gia' creati dal reader (senza O_CREAT).
     *   s_ready = sem_open(SEM_READY, 0);
     *   s_done  = sem_open(SEM_DONE,  0);          */

    printf("[writer] in attesa che il reader sia pronto...\n");
    /* TODO: sem_wait(s_ready) */

    printf("[writer] scrivo %d interi in %s\n", N, DATAFILE);
    /* TODO: apri DATAFILE in scrittura (open + O_WRONLY|O_CREAT),
     *       scrivi N interi casuali con write(), chiudi il file. */

    printf("[writer] scrittura completata, segnalo il reader\n");
    /* TODO: sem_post(s_done) */

    sem_close(s_ready);
    sem_close(s_done);
    return 0;
}
