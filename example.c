/* Copyright (C) 2014 John Bartholomew. For licensing terms, see the header file nfa.h */

#define NFA_API static
#include "nfa.c" /* implementation as well as interface */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static Nfa *build_regex(const char *pattern) {
   NfaBuilder builder;
   Nfa *nfa = NULL;
   nfa_builder_init(&builder);
   nfa_build_regex(&builder, pattern, -1, 0);
   /* capture the entire pattern as group 0 */
   nfa_build_capture(&builder, 0);
   nfa = nfa_builder_output(&builder);
   if (builder.error) {
      fprintf(stderr, "error: %s\n", nfa_error_string(builder.error));
      assert(!nfa);
   }
   nfa_builder_free(&builder);
   return nfa;
}

#define MAX_CAPTURES 10

int main(int argc, char **argv) {
   Nfa *nfa;
   NfaCapture captures[MAX_CAPTURES];

   if (argc < 2) {
      fprintf(stderr, "usage: example PATTERN\n");
      return EXIT_FAILURE;
   }

   memset(captures, 0, sizeof(captures));

   nfa = build_regex(argv[1]);
   if (nfa) {
#ifndef NFA_NO_STDIO
      nfa_print_machine(nfa, stdout);
#endif
      if (argc > 2) {
         int i;
         for (i = 2; i < argc; ++i) {
            const int matched = nfa_match(nfa, captures, MAX_CAPTURES, argv[i], -1);
            if (matched < 0) {
               printf("error: %s\n", nfa_error_string(matched));
               continue;
            }
            printf("%s: '%s'\n", (matched ? "   MATCH" : "NO MATCH"), argv[i]);
            if (matched) {
               int j;
               for (j = 0; j < MAX_CAPTURES; ++j) {
                  int b = captures[j].begin;
                  int e = captures[j].end;
                  if (b || e) {
                     printf("capture %d: %d--%d '%.*s'\n", j, b, e, e - b, argv[i] + b);
                  }
               }
            }
         }
      }
      free(nfa);
   }
   return EXIT_SUCCESS;
}
/* vim: set ts=8 sts=3 sw=3 et: */
