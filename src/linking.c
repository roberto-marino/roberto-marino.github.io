#include <stdio.h>

int globale = 42;
static int locale = 99;
const int costante = 7;
int non_inizializzato;

static void helper(void) {}
void foo(void) { helper(); }

int main(void) {
    printf("ciao\n");
    return 0;
}
