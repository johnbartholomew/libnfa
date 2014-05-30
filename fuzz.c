#define NFA_API static
#include "nfa.c"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* custom allocator to give valgrind a chance to detect problems:
 * by allocating the minimum size, we ensure every allocation gets put in a separate pool,
 * so reads or writes past the end of the allocation (eg, if the number of ops in a fragment
 * is miscalculated) should be caught by valgrind
 */
static void *fuzz_alloc(void *udata, void *p, size_t *size) {
   (void)udata;
   if (p) {
      free(p);
   } else {
      p = malloc(*size);
      *size = (p ? *size : 0u);
   }
   return p;
}

static char BUILDER_POOL[12 << 10];

static int try_build_regex(const char *pattern, size_t length) {
   NfaBuilder builder;
   Nfa *nfa = NULL;
   int error;

#if 0
   nfa_builder_init_custom(&builder, &fuzz_alloc, NULL);
#else
   nfa_builder_init_pool(&builder, BUILDER_POOL, sizeof(BUILDER_POOL));
#endif
   nfa_build_regex(&builder, pattern, length, 0);
   /* capture the entire pattern as group 0 */
   nfa_build_capture(&builder, 0);
   nfa = nfa_builder_output(&builder);
   if (builder.error) { assert(!nfa); }
   error = builder.error;
   nfa_builder_free(&builder);
   free(nfa);
   return error;
}

static const char HEX_DIGITS[] = "0123456789ABCDEF";

static const char *safe_char(char c, char *buf) {
   if (c >= 32 && c < 127) {
      buf[0] = c;
      buf[1] = '\0';
   } else {
      switch (c) {
         case 0x00: return "\\0";
         case 0x07: return "\\a";
         case 0x08: return "\\b";
         case 0x09: return "\\t";
         case 0x0A: return "\\n";
         case 0x0B: return "\\v";
         case 0x0C: return "\\f";
         case 0x0D: return "\\r";
         case 0x1B: return "\\e";
         default:
            buf[0] = '\\';
            buf[1] = 'x';
            buf[2] = HEX_DIGITS[((uint8_t)c >> 4) & 15];
            buf[3] = HEX_DIGITS[((uint8_t)c) & 15];
            buf[4] = '\0';
            break;
      }
   }
   return buf;
}

static void print_input(const char *pattern, size_t length) {
   char buf[8];
   int i;
   for (i = 0; i < (int)length; ++i) {
      fprintf(stderr, "%s", safe_char(pattern[i], buf));
   }
}

/* <pattern>      ::=  <alternation>
 *                ::=
 * <alternation>  ::=  <alternation> '|' <repetition>
 *                ::=  <repetition>
 * <repetition>   ::=  <greedyrep> '?'
 *                ::=  <greedyrep>
 * <greedyrep>    ::=  <term> '?'
 *                ::=  <term> '*'
 *                ::=  <term> '+'
 *                ::=  <term>
 * <term>         ::=  '(' <pattern> ')'
 *                ::=  '[' <charclass> ']'
 *                ::=  '.'
 *                ::=  '^'
 *                ::=  '$'
 *                ::=  <byte>
 * <charclass>    ::=  '^' <charranges>
 *                ::=  <charranges>
 * <charranges>   ::=  <charranges> <charrange>
 *                ::=  <charrange>
 * <charrange>    ::=  <byte> '-' <byte>
 *                ::=  <byte>
 * <byte>         ::=  '\\' ANY
 *                ::=  ANY
 */

enum {
   NODE_PATTERN = 300,
   NODE_ALTERNATION,
   NODE_REPETITION,
   NODE_GREEDYREPETITION,
   NODE_TERM,
   NODE_CHARCLASS,
   NODE_CHARRANGES,
   NODE_CHARRANGE,
   NODE_BYTE,
   NODE_ANYCHAR,
   NODE_MAXID
};

struct FuzzBranch {
   int fromnode;
   int prob;
   int tonodes[8];
};

static const struct FuzzBranch FUZZ_GRAMMAR[] = {
   { NODE_PATTERN, 10, { NODE_ALTERNATION, -1 } },
   { NODE_PATTERN, 1, { -1 } },
   { NODE_ALTERNATION, 1, { NODE_ALTERNATION, '|', NODE_REPETITION, -1 } },
   { NODE_ALTERNATION, 2, { NODE_REPETITION, -1 } },
   { NODE_REPETITION, 1, { NODE_GREEDYREPETITION, '?', -1 } },
   { NODE_REPETITION, 3, { NODE_GREEDYREPETITION, -1 } },
   { NODE_GREEDYREPETITION, 1, { NODE_TERM, '?', -1 } },
   { NODE_GREEDYREPETITION, 1, { NODE_TERM, '*', -1 } },
   { NODE_GREEDYREPETITION, 1, { NODE_TERM, '+', -1 } },
   { NODE_TERM, 6, { '(', NODE_PATTERN, ')', -1 } },
   { NODE_TERM, 2, { '[', NODE_CHARCLASS, ']', -1 } },
   { NODE_TERM, 2, { '.', -1 } },
   { NODE_TERM, 1, { '^', -1 } },
   { NODE_TERM, 1, { '$', -1 } },
   { NODE_TERM, 30, { NODE_BYTE, -1 } },
   { NODE_CHARCLASS, 1, { '^', NODE_CHARRANGES, -1 } },
   { NODE_CHARCLASS, 4, { NODE_CHARRANGES, -1 } },
   { NODE_CHARRANGES, 2, { NODE_CHARRANGES, NODE_CHARRANGE, -1 } },
   { NODE_CHARRANGES, 1, { NODE_CHARRANGE, -1 } },
   { NODE_CHARRANGE, 1, { NODE_BYTE, '-', NODE_BYTE, -1 } },
   { NODE_CHARRANGE, 5, { NODE_BYTE, -1 } },
   { NODE_BYTE, 1, { '\\', NODE_ANYCHAR, -1 } },
   { NODE_BYTE, 6, { NODE_ANYCHAR, -1 } },
   { NODE_ANYCHAR, 1, { '0', -1 } },
   { NODE_ANYCHAR, 1, { '1', -1 } },
   { NODE_ANYCHAR, 1, { '2', -1 } },
   { NODE_ANYCHAR, 1, { '3', -1 } },
   { NODE_ANYCHAR, 1, { 'a', -1 } },
   { NODE_ANYCHAR, 1, { 'b', -1 } },
   { NODE_ANYCHAR, 1, { 'c', -1 } },
   { NODE_ANYCHAR, 1, { 'd', -1 } },
   { NODE_ANYCHAR, 1, { 'A', -1 } },
   { NODE_ANYCHAR, 1, { 'B', -1 } },
   { NODE_ANYCHAR, 1, { 'C', -1 } },
   { NODE_ANYCHAR, 1, { 'D', -1 } },
   { NODE_ANYCHAR, 1, { 'w', -1 } },
   { NODE_ANYCHAR, 1, { 'x', -1 } },
   { NODE_ANYCHAR, 1, { 'y', -1 } },
   { NODE_ANYCHAR, 1, { 'z', -1 } },
   { NODE_ANYCHAR, 1, { 'W', -1 } },
   { NODE_ANYCHAR, 1, { 'X', -1 } },
   { NODE_ANYCHAR, 1, { 'Y', -1 } },
   { NODE_ANYCHAR, 1, { 'Z', -1 } },
   { NODE_MAXID, 0, {0} },
};

static size_t gen_from_grammar(const struct FuzzBranch *grammar, char *out, int node, int maxdepth) {
   const struct FuzzBranch *rule;
   int begin, end, count, which, i, x, y;
   size_t length = 0;

   if (maxdepth <= 0) { return length; }

   for (begin = 0; grammar[begin].fromnode < node; ++begin) {}
   assert(grammar[begin].fromnode == node);

   count = 0;
   for (end = begin; grammar[end].fromnode <= node; ++end) {
      assert(grammar[end].prob >= 0);
      count += grammar[end].prob;
   }
   assert(grammar[end].fromnode > node);

   x = (rand() % count);
   y = 0;
   for (which = begin; which < end; ++which) {
      y += grammar[which].prob;
      if (x < y) { break; }
   }
   assert(grammar[which].fromnode == node);

#if 0
   fprintf(stderr, "node %d, grammar %d--%d, picked %d\n", node, begin, end, which);
#endif

   rule = grammar + which;
   for (i = 0; rule->tonodes[i] >= 0; ++i) {
      x = rule->tonodes[i];
      if (x < 256) {
         *out++ = (char)x;
         ++length;
      } else {
         size_t len = gen_from_grammar(grammar, out, x, maxdepth - 1);
         out += len;
         length += len;
      }
   }
   return length;
}

static void grammarfuzz(int count, int maxdepth) {
   char *input = malloc(maxdepth * 4);
   int error, i;
   for (i = 0; i < count; ++i) {
      size_t length = gen_from_grammar(FUZZ_GRAMMAR, input, NODE_PATTERN, maxdepth);
      error = try_build_regex(input, length);
      fprintf(stderr, "%3s: ", (error ? "BAD" : "OK"));
      print_input(input, length);
      fprintf(stderr, " (%s)\n", nfa_error_string(error));
   }
   free(input);
}

static const char FUZZ_CHARS[] = {
   '(', ')', '*', '+', '?', '[', ']', '-', '^', '$', '.', '\\', '0', '\0',
   'a', 'b', 'c', 'd', 'A', 'B', 'C', 'D', 'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z'
};

static void fuzz(int count, int length) {
   char *input = malloc(length);
   int error, i, j;
   for (i = 0; i < count; ++i) {
      for (j = 0; j < length; ++j) {
         input[j] = FUZZ_CHARS[rand() % sizeof(FUZZ_CHARS)];
      }
      error = try_build_regex(input, length);
      fprintf(stderr, "%3s: ", (error ? "BAD" : "OK"));
      print_input(input, length);
      fprintf(stderr, " (%s)\n", nfa_error_string(error));
   }
   free(input);
}

int main(int argc, char **argv) {
   int i;
   (void)argc;
   (void)argv;
   srand(0x023C01FCu);
   grammarfuzz(100000, 100);
   fuzz(100000, 8);
   fuzz(100000, 14);
   for (i = 10; i <= 80; ++i) {
      fuzz(10000, i);
   }
   return EXIT_SUCCESS;
}
/* vim: set ts=8 sts=3 sw=3 et: */
