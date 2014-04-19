/* libnfa, version ??? (0.9+).  Copright © 2014 John Bartholomew.
 *
 * ----------------- LICENCE -----------------
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * -------------------------------------------
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
#  include <stdio.h>
#endif
#include <stddef.h>
#include <stdint.h>

#ifndef NFA_API
#define NFA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NFA_BUILDER_MAX_STACK
#  define NFA_BUILDER_MAX_STACK  48
#endif

#ifndef NFA_DEFAULT_PAGE_SIZE
/* note: if you increase MAX_STACK, you might want to increase this too */
#  define NFA_DEFAULT_PAGE_SIZE  1024u
#endif

typedef void* (*NfaPageAllocFn)(void *userdata, void *p, size_t *size);

typedef struct NfaPoolAllocator {
   NfaPageAllocFn allocf;
   void *userdata;
   void *head;
} NfaPoolAllocator;

typedef struct Nfa Nfa;

typedef struct NfaCapture {
   int begin;
   int end;
} NfaCapture;

typedef struct NfaBuilder {
   void *data; /* private data */
   NfaPoolAllocator alloc;
   int error;
} NfaBuilder;

enum NfaReturnCode {
   NFA_NO_ERROR = 0,

   NFA_RESULT_NOMATCH = 0,
   NFA_RESULT_MATCH   = 1,

   NFA_ERROR_OUT_OF_MEMORY           = -1,
   NFA_ERROR_NFA_TOO_LARGE           = -2,
   NFA_ERROR_STACK_OVERFLOW          = -3,
   NFA_ERROR_STACK_UNDERFLOW         = -4,
   NFA_ERROR_REPETITION_OF_EMPTY_NFA = -5,
   NFA_ERROR_COMPLEMENT_OF_NON_CHAR  = -6,
   NFA_ERROR_UNCLOSED                = -7,
   NFA_ERROR_BUFFER_TOO_SMALL        = -8,
   NFA_MAX_ERROR                     = -9
};

enum NfaBuildFlag {
   /* case-insensitive matches (only for ASCII chars) */
   NFA_MATCH_CASE_INSENSITIVE = 1,
   NFA_REPEAT_NON_GREEDY = 1
};

typedef struct NfaMachine {
   void *data; /* private data */
   NfaPoolAllocator alloc;
   const Nfa *nfa;
   NfaCapture *captures;
   int ncaptures;
   int error;
} NfaMachine;

enum NfaExecContextFlag {
   NFA_EXEC_AT_START = (1u << 0),
   NFA_EXEC_AT_END   = (1u << 1),
   NFA_EXEC_USERBASE = (1u << 2)  /* define your own context flags as: FLAG_i = (NFA_EXEC_USERBASE << i) */
};

/* return a (statically allocated, English) description for an NfaReturnCode */
NFA_API const char *nfa_error_string(int error);

/* simple NFA execution API */
NFA_API int nfa_match(const Nfa *nfa, NfaCapture *captures, int ncaptures, const char *text, size_t length);

/* full NFA execution API */
NFA_API int nfa_exec_init(NfaMachine *vm, const Nfa *nfa, int ncaptures);
NFA_API int nfa_exec_init_pool(NfaMachine *vm, const Nfa *nfa, int ncaptures, void *pool, size_t pool_size);
NFA_API int nfa_exec_init_custom(NfaMachine *vm, const Nfa *nfa, int ncaptures, NfaPageAllocFn allocf, void *userdata);
NFA_API void nfa_exec_free(NfaMachine *vm);

NFA_API int nfa_exec_start(NfaMachine *vm, int location, uint32_t context_flags);
NFA_API int nfa_exec_step(NfaMachine *vm, char byte, int location, uint32_t context_flags);
NFA_API int nfa_exec_match_string(NfaMachine *vm, const char *text, size_t length);

NFA_API int nfa_exec_is_accepted(const NfaMachine *vm); /* returns 0 if the machine is in an error state */
NFA_API int nfa_exec_is_rejected(const NfaMachine *vm); /* returns 1 if the machine is in an error state */
NFA_API int nfa_exec_is_finished(const NfaMachine *vm); /* rejected || accepted */

#ifndef NFA_NO_STDIO
NFA_API void nfa_print_machine(const Nfa *nfa, FILE *to);
#endif
NFA_API size_t nfa_size(const Nfa *nfa);

/* initialise a builder */
NFA_API int nfa_builder_init(NfaBuilder *builder);
NFA_API int nfa_builder_init_pool(NfaBuilder *builder, void *pool, size_t pool_size);
NFA_API int nfa_builder_init_custom(NfaBuilder *builder, NfaPageAllocFn allocf, void *userdata);
/* free all builder resources */
NFA_API void nfa_builder_free(NfaBuilder *builder);

/* compile an NFA and return it */
NFA_API Nfa *nfa_builder_output(NfaBuilder *builder);
NFA_API size_t nfa_builder_output_size(NfaBuilder *builder);
NFA_API int nfa_builder_output_to_buffer(NfaBuilder *builder, Nfa *nfa, size_t size);

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

NFA_API int nfa_build_complement_char(NfaBuilder *builder); /* pop char-matcher 'e', push [^e] */

/* sub-match capture */
NFA_API int nfa_build_capture(NfaBuilder *builder, int id); /* pop expression 'e', push capture '(e)' */

/* assertions (matchers which do not consume input) */
NFA_API int nfa_build_assert_at_start(NfaBuilder *builder); /* push a '^' assertion */
NFA_API int nfa_build_assert_at_end(NfaBuilder *builder); /* push a '$' assertion */
NFA_API int nfa_build_assert_context(NfaBuilder *builder, uint32_t flag);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NFA_H */
/* vim: set ts=8 sts=3 sw=3 et: */
