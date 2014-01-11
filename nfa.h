/* Simple NFA implementation.
 *
 * This library is potentially useful if you need something like
 * regular expressions, but not using typical regex syntax. I'm
 * using it for shell style wildcard matching, because that's
 * basically regex with a different syntax.
 *
 */
#ifndef NFA_H
#define NFA_H

#ifndef NFA_NO_STDIO
#include <stdio.h>
#endif
#include <stddef.h>
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

typedef struct NfaCapture {
   int begin;
   int end;
} NfaCapture;

#define NFA_BUILDER_MAX_STACK 256

typedef struct NfaBuilder {
   /* private */ struct NfaiFragment *stack[NFA_BUILDER_MAX_STACK];
   /* private */ int frag_size[NFA_BUILDER_MAX_STACK];
   /* private */ int nstack;

   /*  public */ int error;
} NfaBuilder;

enum NfaBuilderError {
   NFA_NO_ERROR = 0,
   NFA_ERROR_OUT_OF_MEMORY,
   NFA_ERROR_NFA_TOO_LARGE,
   NFA_ERROR_STACK_OVERFLOW,
   NFA_ERROR_STACK_UNDERFLOW,
   NFA_ERROR_REPETITION_OF_EMPTY_NFA,
   NFA_ERROR_UNCLOSED,

   NFA_MAX_ERROR
};

enum NfaMatchFlag {
   /* case-insensitive matches (only for ASCII chars) */
   NFA_MATCH_CASE_INSENSITIVE = 1,
   NFA_REPEAT_NON_GREEDY = 1
};

enum NfaExecContextFlag {
   NFA_EXEC_AT_START = (1u << 0),
   NFA_EXEC_AT_END   = (1u << 1),
   NFA_EXEC_USERBASE = (1u << 2)  /* define your own context flags as: FLAG_i = (NFA_EXEC_USERBASE << i) */
};

/* simple NFA execution API */
NFA_API int nfa_match(const Nfa *nfa, NfaCapture *captures, int ncaptures, const char *text, size_t length);

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
NFA_API Nfa *nfa_builder_finish(NfaBuilder *builder);

/* NFAs are built using a stack discipline */

/* matchers (push a matcher onto the stack) */
NFA_API int nfa_build_match_empty(NfaBuilder *builder);
NFA_API int nfa_build_match_string(NfaBuilder *builder, const char *bytes, size_t length, int flags);
NFA_API int nfa_build_match_byte(NfaBuilder *builder, char c, int flags);
NFA_API int nfa_build_match_byte_range(NfaBuilder *builder, char first, char last, int flags);
NFA_API int nfa_build_match_any(NfaBuilder *builder);

/* operators */
NFA_API int nfa_build_join(NfaBuilder *builder); /* pop two expressions, push their concatenation */
NFA_API int nfa_build_alt(NfaBuilder *builder);  /* pop two expressions, push their alternation */
NFA_API int nfa_build_zero_or_one(NfaBuilder *builder, int flags);  /* pop expression 'e', push 'e?' */
NFA_API int nfa_build_zero_or_more(NfaBuilder *builder, int flags); /* pop expression 'e', push 'e*' */
NFA_API int nfa_build_one_or_more(NfaBuilder *builder, int flags);  /* pop expression 'e', push 'e+' */

/* sub-match capture */
NFA_API int nfa_build_capture(NfaBuilder *builder, int id); /* pop expression 'e', push capture '(e)' */

/* assertions (matchers which do not consume input) */
NFA_API int nfa_build_assert_at_start(NfaBuilder *builder); /* push a '^' assertion */
NFA_API int nfa_build_assert_at_end(NfaBuilder *builder); /* push a '$' assertion */
NFA_API int nfa_build_assert_context(NfaBuilder *builder, uint32_t flag);
NFA_API int nfa_build_assert_before(NfaBuilder *builder); /* pop expression, push it as a look-ahead assertion */
NFA_API int nfa_build_assert_after(NfaBuilder *builder);  /* pop expression, push it as a look-behind assertion */
NFA_API int nfa_build_assert_boundary(NfaBuilder *builder); /* pop two expressions, push an assertion that the input is between the two expressions (typically used to implement word-boundary detection, etc) */

#ifdef NFA_IMPLEMENTATION
#include "nfa.c"
#endif

#endif /* NFA_H */
/* vim: set ts=8 sts=3 sw=3 et: */
