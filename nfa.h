/* Simple NFA implementation.
 *
 * This library is potentially useful if you need something like
 * regular expressions, but not using typical regex syntax.
 */
#ifndef NFA_H
#define NFA_H

#ifndef NFA_NO_STDIO
#include <stdio.h>
#endif
#include <stdint.h>

#ifndef NFA_API
#define NFA_API
#endif

typedef uint16_t NfaOpcode;
#define NFA_MAX_OPS  (UINT16_MAX-1)

typedef struct Nfa {
   int nops;
   NfaOpcode ops[1];
} Nfa;

#define NFA_BUILDER_MAX_STACK 256

typedef struct NfaiBuilder {
   struct NfaiFragment *stack[NFA_BUILDER_MAX_STACK];
   int frag_size[NFA_BUILDER_MAX_STACK];
   int nstack;
   int error;
} NfaBuilder;

enum NfaBuilderError {
   NFA_NO_ERROR = 0,
   NFA_ERROR_OUT_OF_MEMORY,
   NFA_ERROR_NFA_TOO_LARGE,
   NFA_ERROR_STACK_OVERFLOW,
   NFA_ERROR_STACK_UNDERFLOW,
   NFA_ERROR_UNCLOSED,

   NFA_MAX_ERROR
};

enum NfaCharMatchFlag {
   /* case-insensitive matches (only for ASCII chars) */
   NFA_MATCH_CASE_INSENSITIVE = 1
};

enum NfaBuilderFinishFlag {
   NFA_BUILD_OPTIMIZE = 1
};

#ifndef NFA_NO_STDIO
NFA_API void nfa_print_machine(const Nfa *nfa, FILE *to);
#endif

/* return a (statically allocated, English) description for an NfaBuilderError */
NFA_API const char *nfa_builder_error_string(int error);

/* initialise a builder */
NFA_API int nfa_builder_init(NfaBuilder *builder);
/* reset a builder to its initial state, freeing any allocated resources */
NFA_API int nfa_builder_reset(NfaBuilder *builder);
/* finialise an NFA, return it, and reset the builder */
NFA_API Nfa *nfa_builder_finish(NfaBuilder *builder, int flags);

/* NFAs are built using a stack discipline */

/* matchers (push a matcher onto the stack) */
NFA_API int nfa_build_match_string(NfaBuilder *builder, const char *bytes, size_t length, int flags);
NFA_API int nfa_build_match_byte(NfaBuilder *builder, char c, int flags);
NFA_API int nfa_build_match_byte_range(NfaBuilder *builder, char first, char last, int flags);
NFA_API int nfa_build_match_any(NfaBuilder *builder);

/* operators */
NFA_API int nfa_build_join(NfaBuilder *builder); /* pop two expressions, push their concatenation */
NFA_API int nfa_build_alt(NfaBuilder *builder);  /* pop two expressions, push their alternation */
NFA_API int nfa_build_zero_or_one(NfaBuilder *builder);  /* pop expression 'e', push 'e?' */
NFA_API int nfa_build_zero_or_more(NfaBuilder *builder); /* pop expression 'e', push 'e*' */
NFA_API int nfa_build_one_or_more(NfaBuilder *builder);  /* pop expression 'e', push 'e+' */

/* sub-match capture */
NFA_API int nfa_build_capture(NfaBuilder *builder, int id); /* pop expression 'e', push capture '(e)' */

/* assertions (matchers which do not consume input) */
NFA_API int nfa_build_assert_at_start(NfaBuilder *builder); /* push a '^' assertion */
NFA_API int nfa_build_assert_at_end(NfaBuilder *builder); /* push a '$' assertion */
NFA_API int nfa_build_assert_before(NfaBuilder *builder); /* pop expression, push it as a look-ahead assertion */
NFA_API int nfa_build_assert_after(NfaBuilder *builder);  /* pop expression, push it as a look-behind assertion */
NFA_API int nfa_build_assert_boundary(NfaBuilder *builder); /* pop two expressions, push an assertion that the input is between the two expressions (typically used to implement word-boundary detection, etc) */

#ifdef NFA_IMPLEMENTATION
#include "nfa.c"
#endif

#endif /* NFA_H */
/* vim: set ts=8 sts=3 sw=3 et: */
