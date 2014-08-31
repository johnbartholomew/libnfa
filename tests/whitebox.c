/* Copyright (C) 2014 John Bartholomew. For licensing terms, see the header file nfa.h */

#include "nfa.c" /* implementation as well as interface */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static char BUILDER_POOL[4096];

static Nfa *test_ci_classes(NfaBuilder *b) {
   nfa_build_match_string(b, "Hello, World!", -1, NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_match_byte_range(b, '[', ']', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'a', 'f', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'N', 'Z', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'n', '}', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'X', 'c', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'N', 'm', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   nfa_build_match_byte_range(b, 'N', 'p', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_join(b);
   return nfa_builder_output(b);
}

static Nfa *test_merge_classes(NfaBuilder *b) {
   nfa_build_match_byte(b, 'a', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_match_byte(b, 'c', 0);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'd', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'b', 0);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'f', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'e', 0);
   nfa_build_alt(b);
   return nfa_builder_output(b);
}

static Nfa* test_negate_classes(NfaBuilder *b) {
   nfa_build_match_byte(b, 'a', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_match_byte(b, 'c', 0);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'd', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'b', 0);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'f', NFA_MATCH_CASE_INSENSITIVE);
   nfa_build_alt(b);
   nfa_build_match_byte(b, 'e', 0);
   nfa_build_alt(b);
   nfa_build_complement_char(b);
   return nfa_builder_output(b);
}

typedef Nfa* (*TestFn)(NfaBuilder*);

static const struct {
   char *name;
   TestFn fn;
} TESTS[] = {
   { "ci classes", test_ci_classes },
   { "class combining", test_merge_classes },
   { "class complement", test_negate_classes },
   { 0, 0 }
};

int main(void) {
   Nfa *nfa;
   NfaBuilder builder;
   int i;

   for (i = 0; TESTS[i].name; ++i) {
      fprintf(stderr, "test '%s':\n", TESTS[i].name);
      nfa_builder_init_pool(&builder, BUILDER_POOL, sizeof(BUILDER_POOL));
      nfa = TESTS[i].fn(&builder);
      if (!nfa) {
         fprintf(stderr, "error: %s\n", nfa_error_string(builder.error));
      }
      nfa_builder_free(&builder);
      if (nfa) {
         nfa_print_machine(nfa, stdout);
         free(nfa);
      }
   }

   return 0;
}
/* vim: set ts=8 sts=3 sw=3 et: */
