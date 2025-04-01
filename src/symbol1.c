// esempio.c
#include <stdio.h>

int a;            // Variabile globale (non inizializzata)
static int b = 10;  // Variabile static globale

void funzione() {
    static int c = 5; // Variabile static locale
    int d = 20;              // Variabile locale automatica
    c++;
    d++;
}

int main() {
    funzione();
    return 0;
}
