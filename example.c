#define NFA_IMPLEMENTATION
#include "nfa.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* regex syntax:
 *           group:  '(' e ')'
 *          normal:  any non-special char
 *             any:  '.'
 *          anchor:  '^' | '$'
 *      repetition:  e ( '?' | '*' | '+' )
 *   concatenation:  e e
 *     alternation:  e '|' e
 */

enum ParseState {
   STATE_NONEMPTY     = (1u << 0),
   STATE_JOIN         = (1u << 1),
   STATE_ALT          = (1u << 2), /* alternation */
   STATE_ALT_EMPTY    = (1u << 3), /* alternation with empty */
   STATE_ALT_NONEMPTY = (1u << 4), /* alternation with non-empty */

   STATE_MASK         = 0x1Fu
};

#define DEBUG_REGEX_BUILDER 0
#if DEBUG_REGEX_BUILDER
static const char *SMALL_BINARY[] = {
   "00000", "00001", "00010", "00011", "00100", "00101", "00110", "00111",
   "01000", "01001", "01010", "01011", "01100", "01101", "01110", "01111",
   "10000", "10001", "10010", "10011", "10100", "10101", "10110", "10111",
   "11000", "11001", "11010", "11011", "11100", "11101", "11110", "11111"
};

static const char *state_string(int state) {
   if (state < 0 || state > STATE_MASK) {
      return "<bad state>";
   } else {
      return SMALL_BINARY[state];
   }
}
#endif

static Nfa *build_regex(const char *pattern) {
   uint8_t stack[NFA_BUILDER_MAX_STACK];
   int top;
   NfaBuilder builder;
   Nfa *nfa = NULL;
   const char *at;

   nfa_builder_init(&builder);

   memset(stack, 0, sizeof(stack));
   top = 1;
   at = pattern;
   while (!builder.error) {
      const char c = *at++;
#if DEBUG_REGEX_BUILDER
      fprintf(stderr, "top = %d; stack[top] = %s; nstack = %d; c = '%c'\n", top, state_string(stack[top]), builder.nstack, c);
#endif
      assert(top > 0);
      if (c == '\0' || c == ')') {
         /* end group */
         if (top > 1 && c == '\0') {
            fprintf(stderr, "bad regex: unclosed group\n");
            goto finished;
         }
         if (top <= 1 && c == ')') {
            fprintf(stderr, "bad regex: unexpected ')'\n");
            goto finished;
         }
         if (stack[top] & STATE_JOIN) { nfa_build_join(&builder); }

         /* if we're in an alternation, finish it */
         if (stack[top] & STATE_ALT) {
            if (stack[top] & STATE_NONEMPTY) {
               if (stack[top] & STATE_ALT_NONEMPTY) {
                  nfa_build_alt(&builder);
               } else {
                  stack[top] |= STATE_ALT_NONEMPTY;
               }
            } else {
               stack[top] |= STATE_ALT_EMPTY;
            }
            if ((stack[top] & (STATE_ALT_EMPTY | STATE_ALT_NONEMPTY)) == (STATE_ALT_EMPTY | STATE_ALT_NONEMPTY)) {
               nfa_build_zero_or_one(&builder);
            }
         }

         /* if this group is non-empty, mark it in the state above */
         if (stack[top] & STATE_NONEMPTY) {
            assert((stack[top-1] & STATE_JOIN) == 0);
            stack[top-1] = ((stack[top-1] & STATE_NONEMPTY) << 1) | STATE_NONEMPTY;
         }

         /* pop stack */
         stack[top] = 0;
         --top;

         if (c == '\0') { break; }
      } else if (c == '|') {
         /* alternation */
         if (stack[top] & STATE_JOIN) { nfa_build_join(&builder); }

         if ((stack[top] & (STATE_NONEMPTY | STATE_ALT_NONEMPTY)) == (STATE_NONEMPTY | STATE_ALT_NONEMPTY)) {
            nfa_build_alt(&builder);
         }

         stack[top] |= STATE_ALT;
         stack[top] |= ((stack[top] & STATE_NONEMPTY) ? STATE_ALT_NONEMPTY : STATE_ALT_EMPTY);
         stack[top] &= ~(STATE_NONEMPTY | STATE_JOIN);
      } else if (c == '?' || c == '*' || c == '+') {
         /* modifiers */
         if (!(stack[top] & STATE_NONEMPTY)) {
            fprintf(stderr, "bad regex: '%c' cannot occur at the beginning of a group\n", c);
            goto finished;
         }

         if (c == '?') {
            nfa_build_zero_or_one(&builder);
         } else if (c == '*') {
            nfa_build_zero_or_more(&builder);
         } else if (c == '+') {
            nfa_build_one_or_more(&builder);
         }
      } else {
         /* matchers */

         if (stack[top] & STATE_JOIN) {
            assert(stack[top] & STATE_NONEMPTY);
            nfa_build_join(&builder);
            stack[top] &= ~STATE_JOIN;
         }

         if (c == '(') {
            /* group */
            ++top;
            if (top >= NFA_BUILDER_MAX_STACK) {
               fprintf(stderr, "bad regex: groups nested too deep\n");
               goto finished;
            }
            assert(stack[top] == 0);
         } else {
            if (c == '.') {
               nfa_build_match_any(&builder);
            } else if (c == '^') {
               nfa_build_assert_at_start(&builder);
            } else if (c == '$') {
               nfa_build_assert_at_end(&builder);
            } else {
               nfa_build_match_byte(&builder, c, 0);
            }
            /* mark as non-empty, or mark as awaiting join */
            if (stack[top] & STATE_NONEMPTY) {
               stack[top] |= STATE_JOIN;
            } else {
               stack[top] |= STATE_NONEMPTY;
            }
         }
      }
   }

   if (builder.nstack == 0) {
      nfa_build_match_empty(&builder);
   }

   nfa = nfa_builder_finish(&builder, 0);
   if (builder.error) {
      fprintf(stderr, "error during building: %s\n", nfa_builder_error_string(builder.error));
      free(nfa);
      nfa = NULL;
   }

finished:
   nfa_builder_reset(&builder);
   return nfa;
}

int main(int argc, char **argv) {
   Nfa *nfa;

   if (argc < 2) {
      fprintf(stderr, "usage: example PATTERN\n");
      return EXIT_FAILURE;
   }

   nfa = build_regex(argv[1]);
   if (nfa) {
      nfa_print_machine(nfa, stdout);
      free(nfa);
   }
   return EXIT_SUCCESS;
}
/* vim: set ts=8 sts=3 sw=3 et: */
