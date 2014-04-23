#define NFA_API static
#include "nfa.c"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static char BUILDER_POOL[8 << 10];
static char EXEC_POOL[16 << 10];

static Nfa *build_nfa(const char *pattern) {
   NfaBuilder builder;
   Nfa *nfa = NULL;

   assert(pattern);

   nfa_builder_init_pool(&builder, BUILDER_POOL, sizeof(BUILDER_POOL));
   nfa_build_regex(&builder, pattern, -1, 0);
   nfa = nfa_builder_output(&builder);
   if (!nfa) {
      fprintf(stderr, "bug: could not build NFA for regex '%s' (%s)\n", pattern, nfa_error_string(builder.error));
   }
   nfa_builder_free(&builder);
   return nfa;
}

static int match_nfa(const Nfa *nfa, const char *string) {
   NfaMachine exec;
   int result;

   assert(nfa);
   assert(string);

   nfa_exec_init_pool(&exec, nfa, 0, EXEC_POOL, sizeof(EXEC_POOL));
   result = nfa_exec_match_string(&exec, string, -1);
   nfa_exec_free(&exec);

   if (result < 0) {
      fprintf(stderr, "bug: error while executing NFA on input '%s' (%s)\n", string, nfa_error_string(result));
      return -1;
   }

   return result;
}

static void run_tests(FILE *fl) {
   char buf[512];
   char pattern[512];
   Nfa *nfa = NULL;
   int pattern_count = 0, test_count = 0, fail_count = 0, skip_count = 0;

   while (1) {
      size_t len;
      char *line = fgets(buf, sizeof(buf), fl);

      if (!line && feof(fl)) { break; }

      if (!line && ferror(fl)) {
         fprintf(stderr, "read-error reading from file\n");
         break;
      }

      /* skip blank lines and comments */
      if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') { continue; }

      len = strlen(line);
      assert(len > 0);
      if (line[len-1] == '\n') {
         line[len-1] = '\0';
         --len;
      }

      if (line[0] == 'p' && line[1] == ' ') {
         free(nfa);
         strcpy(pattern, line + 2);
         nfa = build_nfa(line + 2);
         ++pattern_count;
         if (!nfa) { ++skip_count; }
         /* nfa_print_machine(nfa, stdout); */
      } else {
         int matched, expected;
         if (line[0] == 'y' && line[1] == ' ') { expected = 1; }
         else if (line[0] == 'n' && line[1] == ' ') { expected = 0; }
         else {
            fprintf(stderr, "could not understand input line:\n%s\n", line);
            continue;
         }

         if (nfa) {
            ++test_count;
            matched = match_nfa(nfa, line + 2);
            if (matched < 0) {
               ++fail_count;
            } else if (matched == expected) {
               /* fprintf(stdout, " ok   (/%s/ %s '%s')\n", pattern, (matched ? "~=" : "~!"), line + 2); */
            } else {
               ++fail_count;
               fprintf(stdout, "FAIL  (/%s/ %s '%s')\n", pattern, (matched ? "~=" : "~!"), line + 2);
            }
         }
      }
   }
   free(nfa);

   fprintf(stdout, "%d patterns (%d skipped)\n", pattern_count, skip_count);
   fprintf(stdout, "%d / %d tests failed\n", fail_count, test_count);
}

int main(int argc, char **argv) {
   FILE *fl;

   if (argc < 2) {
      fprintf(stderr, "usage: blackbox testset\n");
      return 1;
   }

   fl = fopen(argv[1], "r");
   if (!fl) {
      fprintf(stderr, "could not open '%s'\n", argv[1]);
      return 1;
   }

   run_tests(fl);

   fclose(fl);
   return 0;
}
/* vim: set ts=8 sts=3 sw=3 et: */
