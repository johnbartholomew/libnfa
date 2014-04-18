/* Copyright (C) 2014 John Bartholomew. For licensing terms, see the header file nfa.h */

#define NFA_API static
#include "nfa.c" /* implementation as well as interface */

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
 *      char class:  '[' ( character ( '-' character )? )+ ']'
 */

enum ParseState {
   STATE_JOIN         = (1u << 0),
   STATE_ALT          = (1u << 1),
   STATE_CAPTURE      = (1u << 2),
   STATE_MASK         = 0x07u
};

#define DEBUG_REGEX_BUILDER 0
#if DEBUG_REGEX_BUILDER
static const char *SMALL_BINARY[] = {
   "000", "001", "010", "011", "100", "101", "110", "111"
};

static const char *state_string(int state) {
   if (state < 0 || state > STATE_MASK) {
      return "<bad state>";
   } else {
      return SMALL_BINARY[state];
   }
}
#endif

static char escaped_char(const char c) {
   switch (c) {
      case 'r': return '\r';
      case 'n': return '\n';
      case '0': return '\0';
      case 't': return '\t';
      case 'b': return '\b';
      case 'v': return '\v';
      default: return c;
   }
}

static Nfa *build_regex(const char *pattern) {
   uint8_t stack[NFA_BUILDER_MAX_STACK];
   uint8_t captures[NFA_BUILDER_MAX_STACK];
   int top, ncaptures;
   NfaBuilder builder;
   Nfa *nfa = NULL;
   const char *at;

   nfa_builder_init(&builder);

   /* we immediately push a matcher so that there's always one we can join to or alternate with */
   nfa_build_match_empty(&builder);

   memset(stack, 0, sizeof(stack));
   memset(captures, 0, sizeof(captures));
   top = 1;
   ncaptures = 0;
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

         if (stack[top] & STATE_ALT) { nfa_build_alt(&builder); }

         if (stack[top] & STATE_CAPTURE) {
            assert(top > 1);
            nfa_build_capture(&builder, captures[top]);
         }

         /* pop stack */
         stack[top--] = 0;

         /* record the fact that we've got a second expression */
         stack[top] |= STATE_JOIN;

         if (c == '\0') { break; }
      } else if (c == '|') {
         /* alternation */
         if (stack[top] & STATE_JOIN) { nfa_build_join(&builder); }
         if (stack[top] & STATE_ALT) { nfa_build_alt(&builder); }

         /* start a new expression */
         nfa_build_match_empty(&builder);
         stack[top] &= ~STATE_JOIN;
         stack[top] |= STATE_ALT;
      } else if (c == '?' || c == '*' || c == '+') {
         int flags = 0;

         if (*at == '?') {
            ++at;
            flags = NFA_REPEAT_NON_GREEDY;
         }

         if (c == '?') {
            nfa_build_zero_or_one(&builder, flags);
         } else if (c == '*') {
            nfa_build_zero_or_more(&builder, flags);
         } else if (c == '+') {
            nfa_build_one_or_more(&builder, flags);
         }
      } else {
         /* matchers */

         if (stack[top] & STATE_JOIN) {
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
            captures[top] = ++ncaptures;
            stack[top] = STATE_CAPTURE;
            nfa_build_match_empty(&builder);
         } else if (c == '[') {
            int first_range = 1;
            /* character class */
            if (*at == ']') {
               fprintf(stderr, "bad regex: empty character class\n");
               goto finished;
            }
            while (*at != ']') {
               char first = *at++;

               if (first == '\0') {
                  fprintf(stderr, "bad regex: unclosed character class\n");
                  goto finished;
               }

               if (first == '\\') {
                  if (*at == '\0') {
                     fprintf(stderr, "bad regex: escape character at end of pattern");
                     goto finished;
                  }
                  first = escaped_char(*at);
                  ++at;
               }

               if (at[0] == '-' && at[1] != ']' && at[1] != '\0') {
                  char last = at[1];
                  at += 2;
                  if (last == '\\') {
                     if (*at == '\0') {
                        fprintf(stderr, "bad regex: escape character at end of pattern");
                        goto finished;
                     }
                     last = escaped_char(*at);
                     ++at;
                  }
                  nfa_build_match_byte_range(&builder, first, last, 0);
               } else {
                  nfa_build_match_byte(&builder, first, 0);
               }

               if (!first_range) {
                  nfa_build_alt(&builder);
               } else {
                  first_range = 0;
               }
            }
            assert(*at == ']');
            ++at;

            /* mark as non-empty, or mark as awaiting join */
            stack[top] |= STATE_JOIN;
         } else {
            if (c == '.') {
               nfa_build_match_any(&builder);
            } else if (c == '^') {
               nfa_build_assert_at_start(&builder);
            } else if (c == '$') {
               nfa_build_assert_at_end(&builder);
            } else if (c == '\\') {
               if (*at == '\0') {
                  fprintf(stderr, "bad regex: escape character at end of pattern");
                  goto finished;
               }
               nfa_build_match_byte(&builder, escaped_char(*at), 0);
               ++at;
            } else {
               nfa_build_match_byte(&builder, c, 0);
            }
            /* mark as non-empty, or mark as awaiting join */
            stack[top] |= STATE_JOIN;
         }
      }
   }

   /* capture the entire pattern as group 0 */
   nfa_build_capture(&builder, 0);
   nfa = nfa_builder_output(&builder);
   if (builder.error) {
      fprintf(stderr, "error during building: %s\n", nfa_error_string(builder.error));
      free(nfa);
      nfa = NULL;
   }

finished:
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
