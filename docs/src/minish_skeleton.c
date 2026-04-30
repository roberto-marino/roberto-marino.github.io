#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAXLINE  1024
#define MAXARGS   64

/* Tokenizza 'line' in argv[], ritorna il numero di argomenti.
 * Modifica 'line' in place (strtok inserisce '\0' al posto degli spazi). */
static int parse(char *line, char *argv[]) {
    int argc = 0;
    char *tok = strtok(line, " \t\n");
    while (tok && argc < MAXARGS - 1) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;   /* termine obbligatorio per execvp */
    return argc;
}

int main(void) {
    char  line[MAXLINE];
    char *argv[MAXARGS];

    while (1) {

        /* 1. Stampa il prompt e leggi una riga da stdin.
         *    fgets() ritorna NULL su EOF (Ctrl-D): in quel caso esci. */
        printf("minish> ");
        fflush(stdout);
        if (!fgets(line, MAXLINE, stdin))
            break;

        /* 2. Parsing: riempie argv[] e ritorna il numero di token.
         *    Se la riga è vuota o solo spazi, argc == 0: riparte il loop. */
        int argc = parse(line, argv);
        (void)argc;          /* evita warning "unused variable" */
        if (argv[0] == NULL)
            continue;

        /* 3. Built-in: exit
         *    Il confronto va fatto PRIMA di fork(), altrimenti
         *    sarebbe il figlio ad uscire, non la shell. */
        if (strcmp(argv[0], "exit") == 0)
            break;

        /* 4. Crea un processo figlio. */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            /* -- FIGLIO --
             * TODO: esegui il comando con execvp(argv[0], argv).
             *
             * execvp cerca il programma nel PATH, quindi "ls" funziona
             * anche senza scrivere "/bin/ls".
             *
             * Se execvp fallisce (comando non trovato, permessi, ...):
             *   - stampa un messaggio di errore con perror(argv[0])
             *   - esci con exit(127)   ← convenzione POSIX per "not found"
             *
             * IMPORTANTE: se non chiami exit() dopo execvp() fallita,
             * il figlio continua ad eseguire il codice del padre
             * (il loop while), aprendo un secondo prompt annidato.
             */

            /* TODO */

        } else {
            /* -- PADRE --
             * TODO: aspetta che il figlio termini con waitpid().
             *
             * Usa la variabile 'status' per raccogliere il codice
             * di uscita, poi stampalo se diverso da 0.
             *
             * Macro utili:
             *   WIFEXITED(status)   → vero se terminato normalmente
             *   WEXITSTATUS(status) → codice passato a exit()
             *   WIFSIGNALED(status) → vero se ucciso da un segnale
             *   WTERMSIG(status)    → numero del segnale
             */

            int status;
            (void)status;    /* rimuovi questo cast quando usi status */

            /* TODO */
        }
    }

    return 0;
}
