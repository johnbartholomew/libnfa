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

   NFAI_OP_JUMP           = (  9u << 8), /* jump to one place */
   NFAI_OP_FORK           = ( 10u << 8), /* jump to multiple places! */
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
   if (!frag) {
      NFAI_ASSERT(builder->error);
      return builder->error;
   }
   frag->ops[0] = op;
   return 0;
}

NFA_INTERNAL struct NfaiFragment *nfai_link_fragments(struct NfaiFragment *a, struct NfaiFragment *b) {
   NFAI_ASSERT(a != b);
   if (b == &NFAI_EMPTY_FRAGMENT) { return a; }
   if (a == &NFAI_EMPTY_FRAGMENT) { return b; }
   a->prev->next = b;
   b->prev->next = a;
   a->prev = b->prev;
   b->prev = a->prev;
   return a;
}

NFA_INTERNAL const char *NFAI_ERROR_DESC[] = {
   "no error",
   "out of memory",
   "NFA too large",
   "stack overflow",
   "stack underflow",
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
#endif

/* ----- PUBLIC API ----- */

#ifndef NFA_NO_STDIO
NFA_API void nfa_print_machine(const Nfa *nfa, FILE *to) {
   char buf[16];
   int i;
   NFAI_ASSERT(nfa);
   NFAI_ASSERT(to);

   fprintf(to, "NFA with %d opcodes:\n", nfa->nops);
   for (i = 0; i < nfa->nops; ++i) {
      NfaOpcode op = nfa->ops[i];
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
            fprintf(to, "match byte %s%s\n", nfai_quoted_char(op & 0xFFu, buf, sizeof(buf)),
                  ((op & NFAI_OPCODE_MASK) == NFAI_OP_MATCH_BYTE_CI) ? " (case insensitive)" : "");
            break;
         case NFAI_OP_MATCH_CLASS:
            ++i;
            fprintf(to, "match range %s--%s\n",
                  nfai_quoted_char(nfa->ops[i] >> 8, buf, sizeof(buf)/2),
                  nfai_quoted_char(nfa->ops[i] & 0xFFu, buf+sizeof(buf)/2, sizeof(buf)/2));
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
            ++i;
            fprintf(to, "jump %+d (-> %d)\n", (int16_t)nfa->ops[i], i+1+(int16_t)nfa->ops[i]);
            break;
         case NFAI_OP_FORK:
            ++i;
            fprintf(to, "fork %+d (-> %d)\n", (int16_t)nfa->ops[i], i+1+(int16_t)nfa->ops[i]);
            break;
      }
   }
   fprintf(to, "  %4d: ACCEPT\n", i);
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

NFA_API Nfa *nfa_builder_finish(NfaBuilder *builder, int flags) {
   Nfa *nfa;
   struct NfaiFragment *frag, *first;
   int to, nops;

   NFAI_ASSERT(builder);
   NFAI_ASSERT(flags == 0); /* flags are currently unsupported */
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
   NFAI_ASSERT(builder->frag_size[0]);

   nops = builder->frag_size[0];
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
   nfa->nops = to;

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
      for (i = 0; i < length; ++i) {
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
   struct NfaiFragment *fork, *jump, *frag;
   int i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   if (builder->nstack < 2) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 2;
   if (builder->frag_size[i] + 2 > NFAI_MAX_JUMP || builder->frag_size[i+1] > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 2);
   if (!fork) { return builder->error; }
   jump = nfai_new_fragment(builder, 2);
   if (!jump) {
      free(fork);
      return builder->error;
   }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_FORK | (uint8_t)1;
   fork->ops[1] = builder->frag_size[i] + 2;
   jump->ops[0] = NFAI_OP_JUMP;
   jump->ops[1] = builder->frag_size[i+1];

   /* link and put on stack */
   frag = nfai_link_fragments(fork, builder->stack[i]);
   frag = nfai_link_fragments(frag, jump);
   frag = nfai_link_fragments(frag, builder->stack[i+1]);
   builder->stack[i] = frag;
   builder->frag_size[i] += 4 + builder->frag_size[i+1];

   /* pop stack */
   builder->stack[i+1] = NULL;
   builder->frag_size[i+1] = 0;
   --builder->nstack;
   return 0;
}

NFA_API int nfa_build_zero_or_one(NfaBuilder *builder, int flags) {
   struct NfaiFragment *fork;
   int i;

   NFAI_ASSERT(builder);
   NFAI_ASSERT((flags == 0) && "non-greedy repetition is not yet supported");
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   if (builder->frag_size[i] > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 2);
   if (!fork) { return builder->error; }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_FORK | (uint8_t)1;
   fork->ops[1] = builder->frag_size[i];

   /* link and put on stack */
   builder->stack[i] = nfai_link_fragments(fork, builder->stack[i]);
   builder->frag_size[i] += fork->nops;
   return 0;
}

NFA_API int nfa_build_zero_or_more(NfaBuilder *builder, int flags) {
   struct NfaiFragment *fork, *jump, *frag;
   int i;

   NFAI_ASSERT(builder);
   NFAI_ASSERT((flags == 0) && "non-greedy repetition is not yet supported");
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   if (builder->frag_size[i] + 4 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 2);
   if (!fork) { return builder->error; }
   jump = nfai_new_fragment(builder, 2);
   if (!jump) {
      free(fork);
      return builder->error;
   }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_FORK | (uint8_t)1;
   fork->ops[1] = builder->frag_size[i] + 2;
   jump->ops[0] = NFAI_OP_JUMP;
   jump->ops[1] = -(builder->frag_size[i] + 4);

   /* link and put on stack */
   frag = nfai_link_fragments(fork, builder->stack[i]);
   frag = nfai_link_fragments(frag, jump);
   builder->stack[i] = frag;
   builder->frag_size[i] += 4;
   return 0;
}

NFA_API int nfa_build_one_or_more(NfaBuilder *builder, int flags) {
   struct NfaiFragment *fork;
   int i;

   NFAI_ASSERT(builder);
   NFAI_ASSERT((flags == 0) && "non-greedy repetition is not yet supported");
   if (builder->error) { return builder->error; }
   if (builder->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = builder->nstack - 1;

   if (builder->frag_size[i] + 2 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 2);
   if (!fork) { return builder->error; }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_FORK | (uint8_t)1;
   fork->ops[1] = -(builder->frag_size[i] + 2);

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
