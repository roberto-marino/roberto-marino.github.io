extern int globale;

void funz_secondaria() {
    // Questa non può accedere a statica_file (è static in vars.c)
    globale += 2; // Modifica la variabile globale
}
