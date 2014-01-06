#include "nfa.h"

#include <stdlib.h>
#include <string.h>

#define NFA_INTERNAL static

#ifdef NDEBUG
#define NFAI_ASSERT(x)
#else
#define NFAI_ASSERT(x) do{if(!(x)){nfai_assert_fail(__FILE__, __LINE__, #x);}}while(0)
#endif

enum NfaiOpCode {
   NFAI_OPCODE_MASK       = (255u << 8),

   NFAI_OP_NOP            = (  0u << 8),
   NFAI_OP_MATCH_ANY      = (  1u << 8), /* match any byte */
   NFAI_OP_MATCH_BYTE     = (  2u << 8), /* match one byte exactly */
   NFAI_OP_MATCH_BYTE_CI  = (  3u << 8), /* match one byte, special-cased to do a case-insensitive match for ASCII */
   NFAI_OP_MATCH_CLASS    = (  4u << 8), /* match a character class stored as an ordered list of disjoint ranges) */
   NFAI_OP_ASSERT_START   = (  5u << 8), /* assert that we're at the start of the input */
   NFAI_OP_ASSERT_END     = (  6u << 8), /* assert that we're at the end of the input */

   NFAI_OP_SAVE_START     = (  7u << 8), /* save the input position (start of capture) */
   NFAI_OP_SAVE_END       = (  8u << 8), /* save the input position (end of capture) */

   NFAI_OP_JUMP           = (  9u << 8), /* jump to one or more places */

   NFAI_OP_ACCEPT         = ( 10u << 8)
};

#define NFAI_MAX_JUMP  (INT16_MAX-1)

#ifndef NDEBUG
NFA_INTERNAL void nfai_assert_fail(const char *file, int line, const char *predicate) {
#ifndef NFA_NO_STDIO
   fprintf(stderr, "NFA assert failure: %s:%d: %s\n", file, line, predicate);
#endif
   abort();
}
#endif

struct NfaiFragment {
   struct NfaiFragment *prev;
   struct NfaiFragment *next;
   int nops;
   NfaOpcode ops[1];
};

NFA_INTERNAL const struct NfaiFragment NFAI_EMPTY_FRAGMENT = {
   (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT,
   (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT,
   0, { 0 }
};

NFA_INTERNAL struct NfaiFragment *nfai_new_fragment(NfaBuilder *builder, int nops) {
   struct NfaiFragment *frag;
   NFAI_ASSERT(builder);
   NFAI_ASSERT(nops >= 0);
   if (builder->error) { return NULL; }
   if (nops > NFA_MAX_OPS) {
      builder->error = NFA_ERROR_NFA_TOO_LARGE;
      return NULL;
   }

   if (nops > 0) {
      frag = malloc(sizeof(struct NfaiFragment) + (nops - 1)*sizeof(NfaOpcode));
      if (!frag) {
         builder->error = NFA_ERROR_OUT_OF_MEMORY;
         return NULL;
      }

      frag->next = frag;
      frag->prev = frag;
      frag->nops = nops;
      return frag;
   } else {
      return (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT;
   }
}

NFA_INTERNAL struct NfaiFragment *nfai_push_new_fragment(NfaBuilder *builder, int nops) {
   struct NfaiFragment *frag;

   NFAI_ASSERT(builder);

   if (builder->error) { return NULL; }

   if (builder->nstack >= NFA_BUILDER_MAX_STACK) {
      builder->error = NFA_ERROR_STACK_OVERFLOW;
      return NULL;
   }

   frag = nfai_new_fragment(builder, nops);
   if (!frag) { return NULL; }

   builder->stack[builder->nstack] = frag;
   builder->frag_size[builder->nstack] = frag->nops;
   ++builder->nstack;
   return frag;
}

NFA_INTERNAL int nfai_push_single_op(NfaBuilder *builder, uint16_t op) {
   struct NfaiFragment *frag = nfai_push_new_fragment(builder, 1);
   if (frag) { frag->ops[0] = op; }
   return builder->error;
}

NFA_INTERNAL struct NfaiFragment *nfai_link_fragments(struct NfaiFragment *a, struct NfaiFragment *b) {
   NFAI_ASSERT(a);
   NFAI_ASSERT(b);
   if (b == &NFAI_EMPTY_FRAGMENT) { return a; }
   if (a == &NFAI_EMPTY_FRAGMENT) { return b; }
   NFAI_ASSERT(a != b);
   a->prev->next = b;
   b->prev->next = a;
   a->prev = b->prev;
   b->prev = a->prev;
   return a;
}

NFA_INTERNAL int nfai_make_alt(
         NfaBuilder *builder,
         struct NfaiFragment *a, int asize,
         struct NfaiFragment *b, int bsize,
         struct NfaiFragment **out_frag, int *out_size) {
   struct NfaiFragment *fork, *frag;
   NFAI_ASSERT(a);
   NFAI_ASSERT(b);
   NFAI_ASSERT(asize >= 0);
   NFAI_ASSERT(bsize >= 0);
   NFAI_ASSERT((a == &NFAI_EMPTY_FRAGMENT && asize == 0) || (asize > 0));
   NFAI_ASSERT((b == &NFAI_EMPTY_FRAGMENT && bsize == 0) || (bsize > 0));

   if (a == &NFAI_EMPTY_FRAGMENT && b == &NFAI_EMPTY_FRAGMENT) {
      *out_frag = a;
      *out_size = 0;
      return 0;
   }

   NFAI_ASSERT(a != b);

   /* +2 for the jump */
   if ((asize + (bsize ? 2 : 0) > NFAI_MAX_JUMP) || (bsize > NFAI_MAX_JUMP)) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }
   if (asize && bsize) {
      struct NfaiFragment *jump = nfai_new_fragment(builder, 2);
      if (!jump) {
         free(fork);
         return builder->error;
      }
      jump->ops[0] = NFAI_OP_JUMP | (uint8_t)1;
      jump->ops[1] = bsize;
      a = nfai_link_fragments(a, jump);
      asize += jump->nops;
   }

   fork->ops[0] = NFAI_OP_JUMP | (uint8_t)2;
   if (asize) {
      fork->ops[1] = 0;
      fork->ops[2] = asize;
   } else {
      fork->ops[1] = bsize;
      fork->ops[2] = 0;
   }

   frag = nfai_link_fragments(fork, a);
   frag = nfai_link_fragments(frag, b);
   *out_frag = frag;
   *out_size = fork->nops + asize + bsize;
   return 0;
}

NFA_INTERNAL const char *NFAI_ERROR_DESC[] = {
   "no error",
   "out of memory",
   "NFA too large",
   "stack overflow",
   "stack underflow",
   "repetition of an empty pattern",
   "finish running when the stack contains multiple items",
   "unknown error"
};

#ifndef NFA_NO_STDIO
NFA_INTERNAL const char *nfai_quoted_char(int c, char *buf, size_t bufsize) {
   NFAI_ASSERT(c >= 0 && c <= UINT8_MAX);
   /* max length is for '\xFF', which is 7 bytes (including null terminator) */
   NFAI_ASSERT(bufsize >= 7);
   if (c >= 32 && c < 127) {
      sprintf(buf, "'%c'", (char)c);
   } else {
      switch (c) {
         case 0x00: return "'\\0'";
         case 0x07: return "'\\a'";
         case 0x08: return "'\\b'";
         case 0x09: return "'\\t'";
         case 0x0A: return "'\\n'";
         case 0x0B: return "'\\v'";
         case 0x0C: return "'\\f'";
         case 0x0D: return "'\\r'";
         case 0x1B: return "'\\e'";
         default:
            sprintf(buf, "'\\x%02X'", (uint8_t)c);
            break;
      }
   }
   return buf;
}

NFA_INTERNAL int nfai_print_opcode(const Nfa *nfa, int state, FILE *to) {
   char buf1[8], buf2[8];
   NfaOpcode op;
   int i;

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(to);
   NFAI_ASSERT(state >= 0 && state < nfa->nops);

   i = state;
   op = nfa->ops[i];
   fprintf(to, "  %4d: ", i);
   switch (op & NFAI_OPCODE_MASK) {
      case NFAI_OP_NOP:
         fprintf(to, "nop\n");
         break;
      case NFAI_OP_MATCH_ANY:
         fprintf(to, "match any\n");
         break;
      case NFAI_OP_MATCH_BYTE:
      case NFAI_OP_MATCH_BYTE_CI:
         fprintf(to, "match byte %s%s\n", nfai_quoted_char(op & 0xFFu, buf1, sizeof(buf1)),
               ((op & NFAI_OPCODE_MASK) == NFAI_OP_MATCH_BYTE_CI) ? " (case insensitive)" : "");
         break;
      case NFAI_OP_MATCH_CLASS:
         ++i;
         fprintf(to, "match range %s--%s\n",
               nfai_quoted_char(nfa->ops[i] >> 8, buf1, sizeof(buf1)),
               nfai_quoted_char(nfa->ops[i] & 0xFFu, buf2, sizeof(buf2)));
         break;
      case NFAI_OP_ASSERT_START:
         fprintf(to, "assert start\n");
         break;
      case NFAI_OP_ASSERT_END:
         fprintf(to, "assert end\n");
         break;
      case NFAI_OP_SAVE_START:
         fprintf(to, "save start @%d\n", (nfa->ops[i] & 0xFFu));
         break;
      case NFAI_OP_SAVE_END:
         fprintf(to, "save end @%d\n", (nfa->ops[i] & 0xFFu));
         break;
      case NFAI_OP_JUMP:
         {
            int base, n = (nfa->ops[i] & 0xFFu);
            ++i;
            base = i + n;
            if (n == 1) {
               fprintf(to, "jump %+d (-> %d)\n", (int16_t)nfa->ops[i], base+(int16_t)nfa->ops[i]);
            } else {
               int j;
               fprintf(to, "fork\n");
               for (j = 0; j < n; ++j, ++i) {
                  fprintf(to, "           %+d (-> %d)\n",
                        (int16_t)nfa->ops[i], base+(int16_t)nfa->ops[i]);
               }
               --i;
            }
         }
         break;
      case NFAI_OP_ACCEPT:
         fprintf(to, "accept\n");
         break;
   }

   ++i;
   return i;
}
#endif

enum NfaContextFlag {
   NFA_CONTEXT_START = (1 << 0),
   NFA_CONTEXT_END   = (1 << 1)
};

typedef struct NfaMachine {
   const Nfa *nfa;
   struct NfaiStateSet *current;
   struct NfaiStateSet *next;
   NfaCapture *captures;
   int ncaptures;
} NfaMachine;

struct NfaiStateSet {
   int nstates;
   /* data contains two consecutive arrays, each long enough to hold nfa->nops items,
    * the first array maps from state to position,
    * the second array maps from position to state
    * only the first 'nstates' items of the position array are valid
    * the state-set can be cleared by setting 'nstates' to zero (it is not necessary
    * to initialise or clear the two arrays)
    */
   uint16_t data[1];
};

NFA_INTERNAL int nfai_ascii_tolower(int x) {
   /* ASCII 'A' = 65; ASCII 'Z' = 90; ASCII 'a' = 97 */
   return ((x < 65 || x > 90) ? x : x + (97 - 65));
}

NFA_INTERNAL int nfai_is_state_marked(const Nfa *nfa, struct NfaiStateSet *states, int state) {
   int nops, position;
   NFAI_ASSERT(nfa);
   NFAI_ASSERT(states);
   NFAI_ASSERT(states->nstates >= 0 && states->nstates < nfa->nops);
   NFAI_ASSERT(state >= 0 && state < nfa->nops);

   nops = nfa->nops;
   position = states->data[state];
   return ((position < states->nstates) && (states->data[nops + position] == state));
}

NFA_INTERNAL void nfai_mark_state(const Nfa *nfa, struct NfaiStateSet *states, int state) {
   int position;
   NFAI_ASSERT(nfa);
   NFAI_ASSERT(states);
   NFAI_ASSERT(states->nstates >= 0 && states->nstates < nfa->nops);
   NFAI_ASSERT(state >= 0 && state < nfa->nops);

   NFAI_ASSERT(!nfai_is_state_marked(nfa, states, state));

   position = states->nstates++;
   states->data[state] = position;
   states->data[nfa->nops + position] = state;
}

NFA_INTERNAL void nfai_trace_state(NfaMachine *vm, struct NfaiStateSet *states, int state, int fromstate) {
   const NfaOpcode *ops;

   NFAI_ASSERT(vm);
   NFAI_ASSERT(states);
   NFAI_ASSERT(state >= 0 && state < vm->nfa->nops);

   if (nfai_is_state_marked(vm->nfa, states, state)) { return; }
   nfai_mark_state(vm->nfa, states, state);

   ops = (vm->nfa->ops + state);
   if ((ops[0] & NFAI_OPCODE_MASK) == NFAI_OP_JUMP) {
      int base, i, njumps;
      njumps = (ops[0] & 0xFFu);
      base = state + 1 + njumps;
      for (i = 1; i <= njumps; ++i) {
         nfai_trace_state(vm, states, base + (int16_t)ops[i], fromstate);
      }
   } else {
      /* copy the set of saved capture positions */
      int ncaps = vm->ncaptures;
      if (ncaps > 0 && (state != fromstate)) {
         const size_t sz = ncaps * sizeof(NfaCapture);
         memcpy(vm->captures + ncaps*state, vm->captures + ncaps*fromstate, sz);
      }
   }
}

/* ----- PUBLIC API ----- */

NFA_API void nfa_exec_alloc_and_init(NfaMachine *vm, const Nfa *nfa, int ncaptures) {
   size_t size;

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(nfa->nops > 0);
   NFAI_ASSERT(ncaptures >= 0);

   vm->nfa = nfa;
   size = sizeof(struct NfaiStateSet) + (2*nfa->nops - 1)*sizeof(uint16_t);
   vm->current = calloc(1u, size);
   vm->next = calloc(1u, size);
   vm->captures = (ncaptures ? calloc(nfa->nops * ncaptures, sizeof(NfaCapture)) : NULL);
   vm->ncaptures = ncaptures;

   /* mark the entry state(s) */
   nfai_trace_state(vm, vm->current, 0, 0);
}

NFA_API void nfa_exec_free(NfaMachine *vm) {
   if (vm) {
      free(vm->current);
      free(vm->next);
      free(vm->captures);
      memset(vm, 0, sizeof(NfaMachine));
   }
}

NFA_API int nfa_exec_is_accepted(NfaMachine *vm) {
   NFAI_ASSERT(vm);
   NFAI_ASSERT(vm->nfa->ops[vm->nfa->nops - 1] == NFAI_OP_ACCEPT);
   return nfai_is_state_marked(vm->nfa, vm->current, vm->nfa->nops - 1);
}

NFA_API int nfa_exec_is_finished(NfaMachine *vm) {
   NFAI_ASSERT(vm);
   NFAI_ASSERT(vm->current->nstates >= 0);
   return ((vm->current->nstates == 0) || (vm->current->nstates == 1 && nfa_exec_is_accepted(vm)));
}

NFA_API void nfa_exec_step(NfaMachine *vm, int location, char byte, char prev, char next, int flags) {
   char buf[8];
   uint16_t *states;
   int i;
   NFAI_ASSERT(vm);

   fprintf(stderr, "[%2d] %s\n", location, nfai_quoted_char((uint8_t)byte, buf, sizeof(buf)));

   states = (vm->current->data + vm->nfa->nops);
   for (i = 0; i < vm->current->nstates; ++i) {
      int istate;
      uint16_t op, arg;

      istate = states[i];
      NFAI_ASSERT(istate >= 0 && istate < vm->nfa->nops);
      op = vm->nfa->ops[istate] & NFAI_OPCODE_MASK;
      arg = vm->nfa->ops[istate] & 0xFFu;

      if (op == NFAI_OP_JUMP) { continue; }

      nfai_print_opcode(vm->nfa, istate, stderr);
      /* fprintf(stderr, "    %2d: state %3d, op %d(%d)\n", i, istate, op, arg); */

      switch (op) {
         default:
            NFAI_ASSERT(0 && "invalid operation");
         case NFAI_OP_NOP:
            NFAI_ASSERT(0 && "NOP should never be generated");
            break;
         case NFAI_OP_MATCH_ANY:
            nfai_trace_state(vm, vm->next, istate + 1, istate);
            break;
         case NFAI_OP_MATCH_BYTE:
            if (arg == (uint8_t)byte) {
               nfai_trace_state(vm, vm->next, istate + 1, istate);
            }
            break;
         case NFAI_OP_MATCH_BYTE_CI:
            if ((arg == (uint8_t)byte) || (nfai_ascii_tolower(arg) == nfai_ascii_tolower(byte))) {
               nfai_trace_state(vm, vm->next, istate + 1, istate);
            }
            break;
         case NFAI_OP_MATCH_CLASS:
            {
               int i, nranges;
               nranges = arg;
               for (i = 1; i <= nranges; ++i) {
                  uint8_t first = (vm->nfa->ops[istate + i] >> 8);
                  uint8_t last = (vm->nfa->ops[istate + i] & 0xFFu);
                  if ((uint8_t)byte < first) { break; }
                  if ((uint8_t)byte <= last) {
                     nfai_trace_state(vm, vm->next, istate + nranges + 1, istate);
                     break;
                  }
               }
            }
            break;
         case NFAI_OP_ASSERT_START:
            NFAI_ASSERT(0 && "assertions are not yet implemented");
            break;
         case NFAI_OP_ASSERT_END:
            NFAI_ASSERT(0 && "assertions are not yet implemented");
            break;
         case NFAI_OP_SAVE_START:
            if (arg < vm->ncaptures) {
               vm->captures[vm->ncaptures*istate + arg].begin = location;
            }
            nfai_trace_state(vm, vm->next, istate + 1, istate);
            break;
         case NFAI_OP_SAVE_END:
            if (arg < vm->ncaptures) {
               vm->captures[vm->ncaptures*istate + arg].end = location;
            }
            nfai_trace_state(vm, vm->next, istate + 1, istate);
            break;
         case NFAI_OP_ACCEPT:
            /* accept state is sticky */
            nfai_trace_state(vm, vm->next, istate, istate);
            /* don't try any lower priority alternatives */
            vm->current->nstates = 0;
            break;
      }
   }

   vm->current->nstates = 0;

   {
      struct NfaiStateSet *tmp = vm->current;
      vm->current = vm->next;
      vm->next = tmp;
   }
}

NFA_API int nfa_match(const Nfa *nfa, NfaCapture *captures, int ncaptures, const char *text, size_t length) {
   NfaMachine vm;
   size_t i;
   int accepted;

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(ncaptures >= 0);
   NFAI_ASSERT(captures || !ncaptures);
   NFAI_ASSERT(text);
   NFAI_ASSERT(nfa->nops >= 1);

   nfa_exec_alloc_and_init(&vm, nfa, ncaptures);

   accepted = 1;
   if (length == (size_t)(-1)) {
      for (i = 0; text[i]; ++i) {
         if (nfa_exec_is_finished(&vm)) {
            accepted = 0;
            break;
         }
         nfa_exec_step(&vm, i, text[i], 0, 0, 0);
      }
   } else {
      for (i = 0; i < length; ++i) {
         if (nfa_exec_is_finished(&vm)) {
            accepted = 0;
            break;
         }
         nfa_exec_step(&vm, i, text[i], 0, 0, 0);
      }
   }

   accepted = accepted && nfa_exec_is_accepted(&vm);

   nfa_exec_free(&vm);
   return accepted;
}

#ifndef NFA_NO_STDIO
NFA_API void nfa_print_machine(const Nfa *nfa, FILE *to) {
   int i;
   NFAI_ASSERT(nfa);
   NFAI_ASSERT(to);
   fprintf(to, "NFA with %d opcodes:\n", nfa->nops);
   for (i = 0; i < nfa->nops;) {
      i = nfai_print_opcode(nfa, i, to);
   }
   fprintf(to, "------\n");
}
#endif

NFA_API const char *nfa_builder_error_string(int error) {
   if (error < 0 || error >= NFA_MAX_ERROR) { error = NFA_MAX_ERROR; }
   return NFAI_ERROR_DESC[error];
}

NFA_API int nfa_builder_init(NfaBuilder *builder) {
   NFAI_ASSERT(builder);
   memset(builder, 0, sizeof(NfaBuilder));
   return (builder->error = 0);
}

NFA_API int nfa_builder_reset(NfaBuilder *builder) {
   NFAI_ASSERT(builder);
   /* clear the fragment stack */
   if (builder->nstack) {
      struct NfaiFragment *frag, *next;
      int i, n;
      n = builder->nstack;
      for (i = 0; i < n; ++i) {
         frag = next = builder->stack[i];
         NFAI_ASSERT(frag);
         if (frag != &NFAI_EMPTY_FRAGMENT) {
            do {
               struct NfaiFragment *p = next;
               next = p->next;
               NFAI_ASSERT(next);
               free(p);
            } while (next != frag);
         }
         builder->stack[i] = NULL;
         builder->frag_size[i] = 0;
      }
      builder->nstack = 0;
   }
   return (builder->error = 0);
}

NFA_API Nfa *nfa_builder_finish(NfaBuilder *builder) {
   Nfa *nfa;
   struct NfaiFragment *frag, *first;
   int to, nops;

   NFAI_ASSERT(builder);
   if (builder->error) { return NULL; }
   if (builder->nstack == 0) {
      builder->error = NFA_ERROR_STACK_UNDERFLOW;
      return NULL;
   }
   if (builder->nstack > 1) {
      builder->error = NFA_ERROR_UNCLOSED;
      return NULL;
   }

   NFAI_ASSERT(builder->stack[0]);
   NFAI_ASSERT(builder->frag_size[0] >= 0);

   nops = builder->frag_size[0] + 1; /* +1 for the NFAI_OP_ACCEPT at the end */
   nfa = malloc(sizeof(Nfa) + (nops - 1)*sizeof(NfaOpcode));
   if (!nfa) {
      builder->error = NFA_ERROR_OUT_OF_MEMORY;
      return NULL;
   }

   first = frag = builder->stack[0];
   to = 0;
   do {
      memcpy(nfa->ops + to, frag->ops, frag->nops * sizeof(NfaOpcode));
      to += frag->nops;
      frag = frag->next;
   } while (frag != first);
   nfa->ops[to++] = NFAI_OP_ACCEPT;
   nfa->nops = to;
   NFAI_ASSERT(nfa->nops == nops);
   return nfa;
}

NFA_API int nfa_build_match_empty(NfaBuilder *builder) {
   nfai_push_new_fragment(builder, 0);
   return builder->error;
}

NFA_API int nfa_build_match_string(NfaBuilder *builder, const char *bytes, size_t length, int flags) {
   struct NfaiFragment *frag;

   NFAI_ASSERT(builder);

   if (builder->error) { return builder->error; }
   if (length > NFA_MAX_OPS) { return (builder->error = NFA_ERROR_NFA_TOO_LARGE); }

   frag = nfai_push_new_fragment(builder, length ? length : 1);
   if (!frag) {
      NFAI_ASSERT(builder->error);
      return builder->error;
   }

   if (length) {
      NfaOpcode opcode = (flags & NFA_MATCH_CASE_INSENSITIVE) ? NFAI_OP_MATCH_BYTE_CI : NFAI_OP_MATCH_BYTE;
      int i;
      for (i = 0; i < (int)length; ++i) {
         frag->ops[i] = (opcode | (uint8_t)bytes[i]);
      }
   } else {
      frag->ops[0] = NFAI_OP_NOP;
   }
   return 0;
}

NFA_API int nfa_build_match_byte(NfaBuilder *builder, char c, int flags) {
   NfaOpcode opcode = (flags & NFA_MATCH_CASE_INSENSITIVE) ? NFAI_OP_MATCH_BYTE_CI : NFAI_OP_MATCH_BYTE;
   return nfai_push_single_op(builder, opcode | (uint8_t)c);
}

NFA_API int nfa_build_match_byte_range(NfaBuilder *builder, char first, char last, int flags) {
   struct NfaiFragment *frag;

   NFAI_ASSERT(flags == 0); /* case-insensitivity is currently not handled for build_match_byte_range */

   frag = nfai_push_new_fragment(builder, 2);
   if (!frag) {
      NFAI_ASSERT(builder->error);
      return builder->error;
   }

   frag->ops[0] = NFAI_OP_MATCH_CLASS;
   frag->ops[1] = ((uint8_t)first << 8) | (uint8_t)last;
   return 0;
}

NFA_API int nfa_build_match_any(NfaBuilder *builder) {
   return nfai_push_single_op(builder, NFAI_OP_MATCH_ANY);
}

NFA_API int nfa_build_join(NfaBuilder *builder) {
   int i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 2) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 2;

   /* link and put on stack */
   builder->stack[i] = nfai_link_fragments(builder->stack[i], builder->stack[i+1]);
   builder->frag_size[i] += builder->frag_size[i+1];

   /* pop stack */
   builder->stack[i+1] = NULL;
   builder->frag_size[i+1] = 0;
   --builder->nstack;
   return 0;
}

NFA_API int nfa_build_alt(NfaBuilder *builder) {
   struct NfaiFragment *frag = NULL;
   int frag_size = 0, i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 2) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 2;
   nfai_make_alt(builder,
         builder->stack[i], builder->frag_size[i],
         builder->stack[i+1], builder->frag_size[i+1],
         &frag, &frag_size);
   if (builder->error) { return builder->error; }

   builder->stack[i] = frag;
   builder->frag_size[i] = frag_size;
   builder->stack[i+1] = NULL;
   builder->frag_size[i+1] = 0;
   --builder->nstack;
   return 0;
}

NFA_API int nfa_build_zero_or_one(NfaBuilder *builder, int flags) {
   struct NfaiFragment *frag = NULL;
   int frag_size = 0, i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;
   if (builder->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (flags & NFA_REPEAT_NON_GREEDY) {
      nfai_make_alt(builder,
            (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT, 0,
            builder->stack[i], builder->frag_size[i],
            &frag, &frag_size);
   } else {
      nfai_make_alt(builder,
            builder->stack[i], builder->frag_size[i],
            (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT, 0,
            &frag, &frag_size);
   }
   if (builder->error) { return builder->error; }

   builder->stack[i] = frag;
   builder->frag_size[i] = frag_size;
   return 0;
}

NFA_API int nfa_build_zero_or_more(NfaBuilder *builder, int flags) {
   struct NfaiFragment *fork, *jump, *frag;
   int i, x;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   if (builder->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (builder->frag_size[i] + 5 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }
   jump = nfai_new_fragment(builder, 2);
   if (!jump) {
      free(fork);
      return builder->error;
   }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_JUMP | (uint8_t)2;
   fork->ops[1] = fork->ops[2] = 0;
   x = ((flags & NFA_REPEAT_NON_GREEDY) ? 1 : 2);
   fork->ops[x] = builder->frag_size[i] + 2;

   jump->ops[0] = NFAI_OP_JUMP | (uint8_t)1;
   jump->ops[1] = -(builder->frag_size[i] + 5);

   /* link and put on stack */
   frag = nfai_link_fragments(fork, builder->stack[i]);
   frag = nfai_link_fragments(frag, jump);
   builder->stack[i] = frag;
   builder->frag_size[i] += jump->nops + fork->nops;
   return 0;
}

NFA_API int nfa_build_one_or_more(NfaBuilder *builder, int flags) {
   struct NfaiFragment *fork;
   int i, x;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   if (builder->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (builder->frag_size[i] + 3 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_JUMP | (uint8_t)2;
   fork->ops[1] = fork->ops[2] = 0;
   x = ((flags & NFA_REPEAT_NON_GREEDY) ? 2 : 1);
   fork->ops[x] = -(builder->frag_size[i] + 3);

   /* link and put on stack */
   builder->stack[i] = nfai_link_fragments(builder->stack[i], fork);
   builder->frag_size[i] += fork->nops;
   return 0;
}

NFA_API int nfa_build_capture(NfaBuilder *builder, int id) {
   struct NfaiFragment *start, *end, *frag;
   int i;

   NFAI_ASSERT(builder);
   NFAI_ASSERT(id >= 0);
   NFAI_ASSERT(id <= UINT8_MAX);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   start = nfai_new_fragment(builder, 1);
   if (!start) { return builder->error; }
   end = nfai_new_fragment(builder, 1);
   if (!end) {
      free(start);
      return builder->error;
   }

   /* fill in fragments */
   start->ops[0] = NFAI_OP_SAVE_START | (uint8_t)id;
   end->ops[0]   = NFAI_OP_SAVE_END   | (uint8_t)id;

   /* link and put on stack */
   frag = nfai_link_fragments(start, builder->stack[i]);
   builder->stack[i] = nfai_link_fragments(frag, end);
   builder->frag_size[i] += start->nops + end->nops;
   return 0;
}

NFA_API int nfa_build_assert_at_start(NfaBuilder *builder) {
   return nfai_push_single_op(builder, NFAI_OP_ASSERT_START);
}

NFA_API int nfa_build_assert_at_end(NfaBuilder *builder) {
   return nfai_push_single_op(builder, NFAI_OP_ASSERT_END);
}

/* vim: set ts=8 sts=3 sw=3 et: */
