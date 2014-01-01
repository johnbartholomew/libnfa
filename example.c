#define NFA_IMPLEMENTATION
#include "nfa.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
   Nfa *nfa;
   NfaBuilder builder;
   nfa_builder_init(&builder);
   nfa_build_match_string(&builder, "Hello", 5, NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_match_string(&builder, "world", 5, 0);
   nfa_build_alt(&builder);
   nfa = nfa_builder_finish(&builder, 0);
   nfa_builder_reset(&builder);
   nfa_print_machine(nfa, stdout);
   free(nfa);
   return 0;
}
/* vim: set ts=8 sts=3 sw=3 et: */
