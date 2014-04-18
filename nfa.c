/* libnfa, version ??? (0.9+).  Copright © 2014 John Bartholomew.
 * For licensing terms, see the header file nfa.h. */

#include "nfa.h"

#include <stdlib.h>
#include <string.h>

#define NFAI_INTERNAL static

#ifdef __cplusplus
extern "C" {
#endif

#if defined(NFA_NO_STDIO) && defined(NFA_TRACE_MATCH)
#  error "nfa: cannot trace matches without stdio support"
#endif

#if defined(NDEBUG) && !defined(NFA_NDEBUG)
#  define NFA_NDEBUG
#endif

#define NFAI_UNUSED(x) ((void)sizeof(x))

#ifdef NFA_NDEBUG
#define NFAI_ASSERT(x) do{NFAI_UNUSED(!(x));}while(0)
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

   NFAI_OP_ASSERT_CONTEXT = (  5u << 8), /* assert that a particular context flag is set */

   NFAI_OP_SAVE_START     = (  7u << 8), /* save the input position (start of capture) */
   NFAI_OP_SAVE_END       = (  8u << 8), /* save the input position (end of capture) */

   NFAI_OP_JUMP           = (  9u << 8), /* jump to one or more places */

   NFAI_OP_ACCEPT         = ( 10u << 8)
};

#define NFAI_MAX_JUMP  (INT16_MAX-1)

#define NFAI_HI_BYTE(x) (uint8_t)((x) >> 8)
#define NFAI_LO_BYTE(x) (uint8_t)((x) & 0xFFu)

#ifndef NFA_NDEBUG
NFAI_INTERNAL void nfai_assert_fail(const char *file, int line, const char *predicate) {
#ifndef NFA_NO_STDIO
   fprintf(stderr, "NFA assert failure: %s:%d: %s\n", file, line, predicate);
#else
   NFAI_UNUSED(file);
   NFAI_UNUSED(line);
   NFAI_UNUSED(predicate);
#endif
   abort();
}
#endif

struct NfaiPage {
   struct NfaiPage *next;
   size_t size;
   size_t at;
   char data[1];
};

#define NFAI_PAGE_HEAD_SIZE  (offsetof(struct NfaiPage, data))

NFAI_INTERNAL void *nfai_default_allocf(/*@unused@*/void *userdata, void *p, size_t *size) {
   NFAI_UNUSED(userdata);
   NFAI_ASSERT((size && !p) || (p && !size));
   if (p) {
      free(p);
      p = NULL;
   } else {
      size_t sz = NFA_DEFAULT_PAGE_SIZE;
      if (*size > sz) { sz = *size; }
      p = malloc(sz);
      *size = (p ? sz : 0u);
   }
   return p;
}

NFAI_INTERNAL void *nfai_null_allocf(void *userdata, void *p, size_t *size) {
   NFAI_UNUSED(userdata);
   NFAI_UNUSED(p);
   NFAI_ASSERT(size);
   *size = 0u;
   return NULL;
}

NFAI_INTERNAL int nfai_alloc_init_default(NfaPoolAllocator *alloc) {
   NFAI_ASSERT(alloc);
   alloc->allocf = &nfai_default_allocf;
   alloc->userdata = NULL;
   alloc->head = NULL;
   return 0;
}

NFAI_INTERNAL int nfai_alloc_init_pool(NfaPoolAllocator *alloc, void *pool, size_t pool_size) {
   struct NfaiPage *page;

   NFAI_ASSERT(alloc);
   NFAI_ASSERT(pool);

   alloc->allocf = &nfai_null_allocf;
   alloc->userdata = NULL;
   alloc->head = NULL;

   if (pool_size < NFAI_PAGE_HEAD_SIZE) { return NFA_ERROR_OUT_OF_MEMORY; }

   alloc->head = pool;

   page = (struct NfaiPage*)pool;
   page->next = NULL;
   page->size = pool_size - NFAI_PAGE_HEAD_SIZE;
   page->at = 0;
   return 0;
}

NFAI_INTERNAL int nfai_alloc_init_custom(NfaPoolAllocator *alloc, NfaPageAllocFn allocf, void *userdata) {
   NFAI_ASSERT(alloc);
   NFAI_ASSERT(allocf);
   alloc->allocf = allocf;
   alloc->userdata = userdata;
   alloc->head = NULL;
   return 0;
}

NFAI_INTERNAL void *nfai_alloc_page(NfaPoolAllocator *pool, size_t min_size) {
   struct NfaiPage *page;
   void *p;
   size_t sz;

   NFAI_ASSERT(pool);
   NFAI_ASSERT(pool->allocf);

   min_size += NFAI_PAGE_HEAD_SIZE;
   sz = min_size;
   p = pool->allocf(pool->userdata, NULL, &sz);
   if (sz < min_size) {
      pool->allocf(pool->userdata, p, NULL);
      return NULL;
   }

   page = (struct NfaiPage*)p;
   page->next = (struct NfaiPage*)pool->head;
   page->size = sz - NFAI_PAGE_HEAD_SIZE;
   page->at = 0;
   pool->head = p;
   return p;
}

NFAI_INTERNAL void *nfai_alloc(NfaPoolAllocator *pool, size_t sz) {
   struct NfaiPage *page;
   size_t free_size;
   void *p;

   NFAI_ASSERT(pool);
   NFAI_ASSERT(sz > 0);

   page = (struct NfaiPage*)pool->head;
   free_size = (page ? page->size - page->at : 0u);
   if (free_size < sz) { page = (struct NfaiPage*)nfai_alloc_page(pool, sz); }
   if (!page) { return NULL; }

   NFAI_ASSERT(page->at <= page->size);
   NFAI_ASSERT(page->size - page->at >= sz);

   p = page->data + page->at;
   page->at += sz;
   return p;
}

NFAI_INTERNAL void *nfai_zalloc(NfaPoolAllocator *pool, size_t sz) {
   void *p = nfai_alloc(pool, sz);
   if (p) { memset(p, 0, sz); }
   return p;
}

NFAI_INTERNAL void nfai_free_pool(NfaPoolAllocator *pool) {
   struct NfaiPage *page, *next;

   NFAI_ASSERT(pool);
   NFAI_ASSERT(pool->allocf);

   if (pool->allocf != &nfai_null_allocf) {
      page = (struct NfaiPage*)pool->head;
      while (page) {
         next = page->next;
         pool->allocf(pool->userdata, page, NULL);
         page = next;
      }
      pool->head = NULL;
   }
}

struct NfaiBuilderData {
   struct NfaiFragment *stack[NFA_BUILDER_MAX_STACK];
   int frag_size[NFA_BUILDER_MAX_STACK];
   int nstack;
};

struct NfaiFragment {
   struct NfaiFragment *prev;
   struct NfaiFragment *next;
   int nops;
   NfaOpcode ops[1];
};

NFAI_INTERNAL const struct NfaiFragment NFAI_EMPTY_FRAGMENT = {
   (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT,
   (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT,
   0, { 0 }
};

#define NFAI_FRAGMENT_SIZE(nops) (sizeof(struct NfaiFragment)+((nops)-1)*sizeof(NfaOpcode))

NFAI_INTERNAL struct NfaiFragment *nfai_new_fragment(NfaBuilder *builder, int nops) {
   struct NfaiFragment *frag;
   NFAI_ASSERT(builder);
   NFAI_ASSERT(nops >= 0);
   if (builder->error) { return NULL; }
   if (nops > NFA_MAX_OPS) {
      builder->error = NFA_ERROR_NFA_TOO_LARGE;
      return NULL;
   }

   if (nops > 0) {
      frag = (struct NfaiFragment*)nfai_alloc(&builder->alloc, NFAI_FRAGMENT_SIZE(nops));
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

NFAI_INTERNAL struct NfaiFragment *nfai_push_new_fragment(NfaBuilder *builder, int nops) {
   struct NfaiFragment *frag;
   struct NfaiBuilderData *data;

   NFAI_ASSERT(builder);

   if (builder->error) { return NULL; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack >= NFA_BUILDER_MAX_STACK) {
      builder->error = NFA_ERROR_STACK_OVERFLOW;
      return NULL;
   }

   frag = nfai_new_fragment(builder, nops);
   if (!frag) { return NULL; }

   data->stack[data->nstack] = frag;
   data->frag_size[data->nstack] = frag->nops;
   ++data->nstack;
   return frag;
}

NFAI_INTERNAL int nfai_push_single_op(NfaBuilder *builder, uint16_t op) {
   struct NfaiFragment *frag = nfai_push_new_fragment(builder, 1);
   if (frag) { frag->ops[0] = op; }
   return builder->error;
}

NFAI_INTERNAL struct NfaiFragment *nfai_link_fragments(struct NfaiFragment *a, struct NfaiFragment *b) {
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

/* ASCII 'A' = 65; ASCII 'Z' = 90; ASCII 'a' = 97; ASCII 'z' = 122 */
NFAI_INTERNAL int nfai_is_ascii_alpha_upper(int x) { return (x >= 65 && x <= 90); }
NFAI_INTERNAL int nfai_is_ascii_alpha_lower(int x) { return (x >= 97 && x <= 122); }

NFAI_INTERNAL int nfai_is_ascii_alpha(int x) {
   return nfai_is_ascii_alpha_upper(x) || nfai_is_ascii_alpha_lower(x);
}

NFAI_INTERNAL int nfai_ascii_tolower(int x) {
   return (nfai_is_ascii_alpha_upper(x) ? (x += (97 - 65)) : x);
}

NFAI_INTERNAL NfaOpcode nfai_byte_match_op(char c, int flags) {
   uint16_t op = NFAI_OP_MATCH_BYTE;
   uint8_t arg = c;
   /* only alpha characters can be matched case-insensitively */
   if ((flags & NFA_MATCH_CASE_INSENSITIVE) && nfai_is_ascii_alpha(arg)) {
      op = NFAI_OP_MATCH_BYTE_CI;
      /* always store the lowercase version */
      arg = nfai_ascii_tolower(arg);
   }
   return op | arg;
}

/* a character-class fragment is one which consists of a single action
 * matching a single character */
NFAI_INTERNAL int nfai_is_frag_charclass(struct NfaiFragment *frag) {
   NFAI_ASSERT(frag);
   NFAI_ASSERT(frag->nops >= 0);
   if (frag->nops == 0) { return 0; }
   if (frag->next != frag) { return 0; }
   switch (frag->ops[0] & NFAI_OPCODE_MASK) {
      case NFAI_OP_MATCH_ANY:
      case NFAI_OP_MATCH_BYTE:
      case NFAI_OP_MATCH_BYTE_CI:
         return (frag->nops == 1);
      case NFAI_OP_MATCH_CLASS:
         return (frag->nops == 1 + (int)NFAI_LO_BYTE(frag->ops[0]));
      default:
         return 0;
   }
}

NFAI_INTERNAL int nfai_char_class_to_ranges(struct NfaiFragment *frag, NfaOpcode **ranges, NfaOpcode *buf) {
   uint8_t arg;
   NFAI_ASSERT(frag);
   NFAI_ASSERT(ranges);
   NFAI_ASSERT(buf);
   NFAI_ASSERT(frag->next == frag);
   NFAI_ASSERT(frag->nops > 0);
   arg = NFAI_LO_BYTE(frag->ops[0]);
   switch (frag->ops[0] & NFAI_OPCODE_MASK) {
      case NFAI_OP_MATCH_ANY:
         buf[0] = (0u << 8) | 255u;
         *ranges = buf;
         return 1;
      case NFAI_OP_MATCH_BYTE:
         buf[0] = (arg << 8) | arg;
         *ranges = buf;
         return 1;
      case NFAI_OP_MATCH_BYTE_CI:
         NFAI_ASSERT(nfai_is_ascii_alpha_lower(arg));
         buf[1] = (arg << 8) | arg;
         arg -= (97 - 65); /* convert to upper case */
         buf[0] = (arg << 8) | arg;
         *ranges = buf;
         return 2;
      case NFAI_OP_MATCH_CLASS:
         *ranges = frag->ops + 1;
         return arg;
      default:
         NFAI_ASSERT(0); /* not a valid character-match opcode */
         buf[0] = 0u;
         *ranges = buf;
         return 0;
   }
}

NFAI_INTERNAL int nfai_merge_ranges(NfaOpcode *to, NfaOpcode *a, int an, NfaOpcode *b, int bn) {
   int ai, bi, nranges;
   NfaOpcode latest;

   NFAI_ASSERT(a);
   NFAI_ASSERT(b);
   NFAI_ASSERT(an > 0);
   NFAI_ASSERT(bn > 0);

   nranges = ai = bi = 0;
   latest = ((a[0] < b[0]) ? a[ai++] : b[bi++]);
   while (ai < an || bi < bn) {
      NfaOpcode next;
      if (ai >= an) { next = b[bi++]; }
      else if (bi >= bn) { next = a[ai++]; }
      else { next = ((a[ai] < b[bi]) ? a[ai++] : b[bi++]); }

      NFAI_ASSERT(latest <= next);
      if ((int)NFAI_LO_BYTE(latest) + 1 >= (int)NFAI_HI_BYTE(next)) {
         latest = (latest & 0xFF00u)
                | (NFAI_LO_BYTE(latest) >= NFAI_LO_BYTE(next)
                      ? NFAI_LO_BYTE(latest) : NFAI_LO_BYTE(next));
      } else {
         if (to) { to[nranges] = latest; }
         ++nranges;
         latest = next;
      }
   }

   if (to) { to[nranges] = latest; }
   ++nranges;

   return nranges;
}

NFAI_INTERNAL int nfai_merge_char_classes(NfaBuilder *builder,
      struct NfaiFragment *a, struct NfaiFragment *b,
      struct NfaiFragment **out_frag, int *out_size) {
   struct NfaiFragment *frag;
   NfaOpcode buf1[2], buf2[2]; /* need max. 2 ranges for an NFAI_OP_MATCH_BYTE_CI op */
   int an, bn, nranges;
   NfaOpcode *aranges, *branges;

   NFAI_ASSERT(builder);
   NFAI_ASSERT(a);
   NFAI_ASSERT(b);
   NFAI_ASSERT(out_frag);
   NFAI_ASSERT(out_size);
   NFAI_ASSERT(nfai_is_frag_charclass(a));
   NFAI_ASSERT(nfai_is_frag_charclass(b));

   an = nfai_char_class_to_ranges(a, &aranges, buf1);
   bn = nfai_char_class_to_ranges(b, &branges, buf2);

   NFAI_ASSERT(an >= 1);
   NFAI_ASSERT(bn >= 1);

   /* first work out how large the output will be */
   nranges = nfai_merge_ranges(NULL, aranges, an, branges, bn);

   frag = nfai_new_fragment(builder, 1 + nranges);
   if (!frag) { return builder->error; }

   frag->ops[0] = NFAI_OP_MATCH_CLASS | (uint8_t)nranges;
   /* actually fill in the output buffer this time */
   nfai_merge_ranges(frag->ops + 1, aranges, an, branges, bn);

   *out_frag = frag;
   *out_size = frag->nops;
   return 0;
}

NFAI_INTERNAL int nfai_make_alt(
         NfaBuilder *builder,
         struct NfaiFragment *a, int asize,
         struct NfaiFragment *b, int bsize,
         struct NfaiFragment **out_frag, int *out_size) {
   struct NfaiFragment *fork, *frag;
   NFAI_ASSERT(builder);
   NFAI_ASSERT(a);
   NFAI_ASSERT(b);
   NFAI_ASSERT(asize >= 0);
   NFAI_ASSERT(bsize >= 0);
   NFAI_ASSERT((a == &NFAI_EMPTY_FRAGMENT && asize == 0) || (asize > 0));
   NFAI_ASSERT((b == &NFAI_EMPTY_FRAGMENT && bsize == 0) || (bsize > 0));
   NFAI_ASSERT(out_frag);
   NFAI_ASSERT(out_size);

   if (a == &NFAI_EMPTY_FRAGMENT && b == &NFAI_EMPTY_FRAGMENT) {
      *out_frag = a;
      *out_size = 0;
      return 0;
   }

   NFAI_ASSERT(a != b);

   if (nfai_is_frag_charclass(a) && nfai_is_frag_charclass(b)) {
      return nfai_merge_char_classes(builder, a, b, out_frag, out_size);
   }

   /* +2 for the jump */
   if ((asize + (bsize ? 2 : 0) > NFAI_MAX_JUMP) || (bsize > NFAI_MAX_JUMP)) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }
   if (asize && bsize) {
      struct NfaiFragment *jump = nfai_new_fragment(builder, 2);
      if (!jump) { return builder->error; }
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

NFAI_INTERNAL int nfai_builder_init_internal(NfaBuilder *builder) {
   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }
   builder->data = nfai_alloc(&builder->alloc, sizeof(struct NfaiBuilderData));
   if (!builder->data) { return (builder->error = NFA_ERROR_OUT_OF_MEMORY); }
   memset(builder->data, 0, sizeof(struct NfaiBuilderData));
   return 0;
}

NFAI_INTERNAL const char *NFAI_ERROR_DESC[] = {
   /* NFA_NO_ERROR                      */ "no error",
   /* NFA_ERROR_OUT_OF_MEMORY           */ "out of memory",
   /* NFA_ERROR_NFA_TOO_LARGE           */ "NFA too large",
   /* NFA_ERROR_STACK_OVERFLOW          */ "stack overflow",
   /* NFA_ERROR_STACK_UNDERFLOW         */ "stack underflow",
   /* NFA_ERROR_REPETITION_OF_EMPTY_NFA */ "repetition of an empty pattern",
   /* NFA_ERROR_UNCLOSED                */ "finish running when the stack contains multiple items",
   /* NFA_ERROR_BUFFER_TOO_SMALL        */ "output buffer is too small",
   /* NFA_MAX_ERROR                     */ "unknown error"
};

#ifndef NFA_NO_STDIO
NFAI_INTERNAL const char *nfai_quoted_char(int c, char *buf, size_t bufsize) {
   NFAI_ASSERT(c >= 0 && c <= UINT8_MAX);
   /* max length is for '\xFF', which is 7 bytes (including null terminator)
    * this is an assert because nfai_quoted_char is internal, ie, if bufsize is < 7
    * then that's a bug in the code in this file somewhere */
   NFAI_UNUSED(bufsize); /* unused in NFA_NDEBUG build */
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

NFAI_INTERNAL int nfai_print_opcode(const Nfa *nfa, int state, FILE *to) {
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
         fprintf(to, "match byte %s%s\n", nfai_quoted_char(NFAI_LO_BYTE(op), buf1, sizeof(buf1)),
               ((op & NFAI_OPCODE_MASK) == NFAI_OP_MATCH_BYTE_CI) ? " (case insensitive)" : "");
         break;
      case NFAI_OP_MATCH_CLASS:
         {
            int n = NFAI_LO_BYTE(nfa->ops[i]);
            if (n == 1) {
               ++i;
               fprintf(to, "match range %s--%s (%d--%d)\n",
                     nfai_quoted_char(NFAI_HI_BYTE(nfa->ops[i]), buf1, sizeof(buf1)),
                     nfai_quoted_char(NFAI_LO_BYTE(nfa->ops[i]), buf2, sizeof(buf2)),
                     NFAI_HI_BYTE(nfa->ops[i]), NFAI_LO_BYTE(nfa->ops[i]));
            } else {
               fprintf(to, "match ranges:\n");
               while (n) {
                  --n; ++i;
                  fprintf(to, "            %s--%s (%d--%d)\n",
                     nfai_quoted_char(NFAI_HI_BYTE(nfa->ops[i]), buf1, sizeof(buf1)),
                     nfai_quoted_char(NFAI_LO_BYTE(nfa->ops[i]), buf2, sizeof(buf2)),
                     NFAI_HI_BYTE(nfa->ops[i]), NFAI_LO_BYTE(nfa->ops[i]));
               }
            }
         }
         break;
      case NFAI_OP_ASSERT_CONTEXT:
         fprintf(to, "assert context (flag %d)\n", (1u << NFAI_LO_BYTE(op)));
         break;
      case NFAI_OP_SAVE_START:
         fprintf(to, "save start @%d\n", NFAI_LO_BYTE(nfa->ops[i]));
         break;
      case NFAI_OP_SAVE_END:
         fprintf(to, "save end @%d\n", NFAI_LO_BYTE(nfa->ops[i]));
         break;
      case NFAI_OP_JUMP:
         {
            int base, n = NFAI_LO_BYTE(nfa->ops[i]);
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

struct NfaiMachineData {
   struct NfaiStateSet *current;
   struct NfaiStateSet *next;
   union NfaiFreeCaptureSet *free_capture_sets;
};

struct NfaiCaptureSet {
   int refcount;
   NfaCapture capture[1];
};

union NfaiFreeCaptureSet {
   struct NfaiCaptureSet set;
   union NfaiFreeCaptureSet *next;
};

struct NfaiStateSet {
   int nstates;
   struct NfaiCaptureSet **captures;
   uint16_t *state;
   uint16_t *position;
};

NFAI_INTERNAL struct NfaiStateSet *nfai_make_state_set(NfaPoolAllocator *pool, int nops, int ncaptures) {
   struct NfaiStateSet *ss;
   NFAI_ASSERT(nops > 0);
   NFAI_ASSERT(ncaptures >= 0);
   ss = (struct NfaiStateSet*)nfai_alloc(pool, sizeof(*ss));
   if (!ss) { return NULL; }
   ss->nstates = 0;
   ss->captures = NULL;
   if (ncaptures) {
      ss->captures = (struct NfaiCaptureSet**)nfai_zalloc(pool, nops*sizeof(struct NfaiCaptureSet*));
      if (!ss->captures) { return NULL; }
   }
   ss->state = (uint16_t*)nfai_zalloc(pool, nops*sizeof(uint16_t));
   if (!ss->state) { return NULL; }
   ss->position = (uint16_t*)nfai_zalloc(pool, nops*sizeof(uint16_t));
   if (!ss->position) { return NULL; }
   return ss;
}

NFAI_INTERNAL int nfai_is_state_marked(const Nfa *nfa, struct NfaiStateSet *states, int state) {
   int position;

   NFAI_UNUSED(nfa);

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(states);
   NFAI_ASSERT(states->nstates >= 0 && states->nstates < nfa->nops);
   NFAI_ASSERT(state >= 0 && state < nfa->nops);

   position = states->position[state];
   return ((position < states->nstates) && (states->state[position] == state));
}

NFAI_INTERNAL void nfai_mark_state(const Nfa *nfa, struct NfaiStateSet *states, int state) {
   int position;

   NFAI_UNUSED(nfa);

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(states);
   NFAI_ASSERT(states->nstates >= 0 && states->nstates < nfa->nops);
   NFAI_ASSERT(state >= 0 && state < nfa->nops);
   NFAI_ASSERT(!nfai_is_state_marked(nfa, states, state));

   position = states->nstates++;
   states->position[state] = position;
   states->state[position] = state;
}

NFAI_INTERNAL struct NfaiCaptureSet *nfai_make_capture_set(NfaMachine *vm) {
   struct NfaiCaptureSet *set;
   struct NfaiMachineData *data;

   NFAI_ASSERT(vm);
   NFAI_ASSERT(!vm->error);
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;

   set = (struct NfaiCaptureSet*)(data->free_capture_sets);
   if (set) {
      data->free_capture_sets = data->free_capture_sets->next;
   } else {
      set = (struct NfaiCaptureSet*)nfai_zalloc(&vm->alloc,
            sizeof(struct NfaiCaptureSet) + sizeof(NfaCapture)*(vm->ncaptures - 1));
      if (!set) {
         vm->error = NFA_ERROR_OUT_OF_MEMORY;
         return NULL;
      }
   }

#ifdef NFA_TRACE_MATCH
   fprintf(stderr, "new capture set: %p\n", set);
#endif
   set->refcount = 1;
   return set;
}

NFAI_INTERNAL void nfai_decref_capture_set(NfaMachine *vm, struct NfaiCaptureSet *set) {
   struct NfaiMachineData *data;
   NFAI_ASSERT(vm);
   NFAI_ASSERT(vm->data);
   NFAI_ASSERT(set);
   NFAI_ASSERT(set->refcount > 0);
   data = (struct NfaiMachineData*)vm->data;
#ifdef NFA_TRACE_MATCH
   fprintf(stderr, "decref %p (from %d)\n", set, set->refcount);
#endif
   if (--set->refcount == 0) {
      union NfaiFreeCaptureSet *fset = (union NfaiFreeCaptureSet*)set;
      fset->next = data->free_capture_sets;
      data->free_capture_sets = fset;
   }
}

NFAI_INTERNAL struct NfaiCaptureSet *nfai_make_capture_set_unique(NfaMachine *vm, struct NfaiCaptureSet *from) {
   NFAI_ASSERT(vm);
   NFAI_ASSERT(from);
   NFAI_ASSERT(from->refcount > 0);

#ifdef NFA_TRACE_MATCH
   fprintf(stderr, "make-unique %p (refcount %d)\n", from, from->refcount);
#endif

   if (from->refcount > 1) {
      struct NfaiCaptureSet *to;
      --from->refcount;
      to = nfai_make_capture_set(vm);
      if (!to) {
         ++from->refcount;
         return NULL;
      }
      memcpy(to->capture, from->capture, sizeof(NfaCapture)*(vm->ncaptures));
      return to;
   }

   return from;
}

NFAI_INTERNAL void nfai_assert_no_captures(NfaMachine *vm, struct NfaiStateSet *set) {
#ifndef NFA_NDEBUG
   int i;
   for (i = 0; i < vm->nfa->nops; ++i) {
      NFAI_ASSERT(set->captures[i] == NULL);
   }
#else
   NFAI_UNUSED(vm);
   NFAI_UNUSED(set);
#endif
}

NFAI_INTERNAL void nfai_clear_state_set_captures(NfaMachine *vm, struct NfaiStateSet *states, int begin) {
   int i;
   if (!states->captures) { return; }
   for (i = begin; i < states->nstates; ++i) {
      int istate = states->state[i];
      struct NfaiCaptureSet *set = states->captures[istate];
      if (set) {
         nfai_decref_capture_set(vm, set);
         states->captures[istate] = NULL;
      }
   }
}

NFAI_INTERNAL void nfai_trace_state(NfaMachine *vm, int location, int state, struct NfaiCaptureSet *captures, uint32_t flags) {
   struct NfaiMachineData *data;
   struct NfaiStateSet *states;
   const NfaOpcode *ops;
   uint16_t op;

   NFAI_ASSERT(vm);
   if (vm->error) { return; }
   NFAI_ASSERT(captures || !vm->ncaptures);

   NFAI_ASSERT(vm->data);

   data = (struct NfaiMachineData*)vm->data;
   states = data->next;

   NFAI_ASSERT(states);
   NFAI_ASSERT(state >= 0 && state < vm->nfa->nops);

   if (nfai_is_state_marked(vm->nfa, states, state)) {
      if (captures) { nfai_decref_capture_set(vm, captures); }
      return;
   }
   nfai_mark_state(vm->nfa, states, state);

#ifdef NFA_TRACE_MATCH
   fprintf(stderr, "TRACE: ");
   nfai_print_opcode(vm->nfa, state, stderr);
#endif

   ops = (vm->nfa->ops + state);
   op = (ops[0] & NFAI_OPCODE_MASK);
   if (op == NFAI_OP_JUMP) {
      int base, i, njumps;
      njumps = NFAI_LO_BYTE(ops[0]);
      NFAI_ASSERT(njumps >= 1);
      base = state + 1 + njumps;
      if (captures) { captures->refcount += njumps - 1; }
      for (i = 1; i <= njumps; ++i) {
         nfai_trace_state(vm, location, base + (int16_t)ops[i], captures, flags);
      }
   } else if (op == NFAI_OP_ASSERT_CONTEXT) {
      uint32_t test;
      int bitidx = NFAI_LO_BYTE(ops[0]);
      NFAI_ASSERT(bitidx >= 0 && bitidx < 32);
      test = ((uint32_t)1 << bitidx);
#ifdef NFA_TRACE_MATCH
      fprintf(stderr, "assert context & %u (%s)\n", test, ((flags & test) ? "passed" : "failed"));
#endif
      if (flags & test) {
         nfai_trace_state(vm, location, state + 1, captures, flags);
      } else {
         if (captures) { nfai_decref_capture_set(vm, captures); }
      }
   } else if (op == NFAI_OP_SAVE_START || op == NFAI_OP_SAVE_END) {
      struct NfaiCaptureSet *set = captures;
      if (captures) {
         int idx = NFAI_LO_BYTE(ops[0]);
         if (idx < vm->ncaptures) {
            set = nfai_make_capture_set_unique(vm, captures);
            if (!set) { NFAI_ASSERT(vm->error); return; }
            if (op == NFAI_OP_SAVE_START) {
               set->capture[idx].begin = location;
            } else {
               set->capture[idx].end = location;
            }
         }
      }
      nfai_trace_state(vm, location, state + 1, set, flags);
   } else {
#ifdef NFA_TRACE_MATCH
      fprintf(stderr, "copying capture %p to state %d\n", captures, state);
#endif
      if (captures) {
         NFAI_ASSERT(states->captures);
         NFAI_ASSERT(captures->refcount > 0);
         states->captures[state] = captures;

         if (op == NFAI_OP_ACCEPT) {
            /* store output captures */
            vm->captures = captures->capture;
         }
      }
   }
}

#if !defined(NFA_NO_STDIO) && defined(NFA_TRACE_MATCH)
NFAI_INTERNAL void nfai_print_captures(FILE *to, const NfaMachine *vm, const struct NfaiStateSet *ss) {
   int i, j;
   NFAI_ASSERT(to);
   NFAI_ASSERT(vm);

   for (i = 0; i < vm->nfa->nops; ++i) {
      struct NfaiCaptureSet *set = ss->captures[i];
      if (set) {
         fprintf(to, "(%p, rc %d) captures for state %2d:", set, set->refcount, i);
         for (j = 0; j < vm->ncaptures; ++j) {
            const NfaCapture *cap = set->capture + j;
            fprintf(to, "  %d--%d", cap->begin, cap->end);
         }
         fprintf(to, "\n");
      }
   }
}
#endif

NFAI_INTERNAL void nfai_swap_state_sets(NfaMachine *vm) {
   struct NfaiMachineData *data;
   struct NfaiStateSet *tmp;

   NFAI_ASSERT(vm);
   if (vm->error) { return; }
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;
   NFAI_ASSERT(data->current);
   NFAI_ASSERT(data->next);

   tmp = data->current;
   data->current = data->next;
   data->next = tmp;
}

NFAI_INTERNAL void nfai_store_captures(const NfaMachine *vm, NfaCapture *captures, int ncaptures) {
   NFAI_ASSERT(vm);
   NFAI_ASSERT(captures || !ncaptures);
   if (ncaptures) {
      if (nfa_exec_is_accepted(vm)) {
         if (ncaptures > vm->ncaptures) {
            memset(captures + vm->ncaptures, 0, (ncaptures - vm->ncaptures) * sizeof(NfaCapture));
            ncaptures = vm->ncaptures;
         }
         memcpy(captures, vm->captures, ncaptures * sizeof(NfaCapture));
      } else {
         memset(captures, 0, ncaptures * sizeof(NfaCapture));
      }
   }
}

NFAI_INTERNAL int nfai_exec_init_internal(NfaMachine *vm, const Nfa *nfa, int ncaptures) {
   struct NfaiMachineData *data;
   NFAI_ASSERT(nfa);
   NFAI_ASSERT(nfa->nops > 0);
   NFAI_ASSERT(ncaptures >= 0);

   data = (struct NfaiMachineData*)nfai_zalloc(&vm->alloc, sizeof(struct NfaiMachineData));
   if (!data) { goto mem_failure; }
   vm->data = data;

   vm->nfa = nfa;
   vm->ncaptures = ncaptures;
   vm->captures = NULL;

   data->current = nfai_make_state_set(&vm->alloc, nfa->nops, ncaptures);
   if (!data->current) { goto mem_failure; }
   data->next = nfai_make_state_set(&vm->alloc, nfa->nops, ncaptures);
   if (!data->next) { goto mem_failure; }
   data->free_capture_sets = NULL;
   return 0;

mem_failure:
   nfai_free_pool(&vm->alloc);
   memset(vm, 0, sizeof(NfaMachine));
   return (vm->error = NFA_ERROR_OUT_OF_MEMORY);
}

/* ----- PUBLIC API ----- */

NFA_API const char *nfa_error_string(int error) {
   if (error > 0) { error = 0; }
   if (error < NFA_MAX_ERROR) { error = NFA_MAX_ERROR; }
   return NFAI_ERROR_DESC[-error];
}

NFA_API int nfa_exec_init(NfaMachine *vm, const Nfa *nfa, int ncaptures) {
   NFAI_ASSERT(vm);
   memset(vm, 0, sizeof(NfaMachine));
   nfai_alloc_init_default(&vm->alloc);
   return nfai_exec_init_internal(vm, nfa, ncaptures);
}

NFA_API int nfa_exec_init_pool(NfaMachine *vm, const Nfa *nfa, int ncaptures, void *pool, size_t pool_size) {
   NFAI_ASSERT(vm);
   memset(vm, 0, sizeof(NfaMachine));
   nfai_alloc_init_pool(&vm->alloc, pool, pool_size);
   return nfai_exec_init_internal(vm, nfa, ncaptures);
}

NFA_API int nfa_exec_init_custom(NfaMachine *vm, const Nfa *nfa, int ncaptures, NfaPageAllocFn allocf, void *userdata) {
   NFAI_ASSERT(vm);
   memset(vm, 0, sizeof(NfaMachine));
   nfai_alloc_init_custom(&vm->alloc, allocf, userdata);
   return nfai_exec_init_internal(vm, nfa, ncaptures);
}

NFA_API void nfa_exec_free(NfaMachine *vm) {
   if (!vm) { return; }
   if (!vm->alloc.allocf) { return; }
   nfai_free_pool(&vm->alloc);
   memset(vm, 0, sizeof(NfaMachine));
}

NFA_API int nfa_exec_is_accepted(const NfaMachine *vm) {
   struct NfaiMachineData *data;
   NFAI_ASSERT(vm);
   if (vm->error) { return 0; }
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;
   NFAI_ASSERT(vm->nfa->ops[vm->nfa->nops - 1] == NFAI_OP_ACCEPT);
   return nfai_is_state_marked(vm->nfa, data->current, vm->nfa->nops - 1);
}

NFA_API int nfa_exec_is_rejected(const NfaMachine *vm) {
   struct NfaiMachineData *data;
   NFAI_ASSERT(vm);
   if (vm->error) { return 1; }
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;
   NFAI_ASSERT(data->current->nstates >= 0);
   return (data->current->nstates == 0);
}

NFA_API int nfa_exec_is_finished(const NfaMachine *vm) {
   return (nfa_exec_is_rejected(vm) || nfa_exec_is_accepted(vm));
}

NFA_API int nfa_exec_start(NfaMachine *vm, int location, uint32_t context_flags) {
   struct NfaiMachineData *data;
   struct NfaiCaptureSet *set;

   NFAI_ASSERT(vm);
   if (vm->error) { return vm->error; }
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;

   /* clear any existing captures */
   nfai_assert_no_captures(vm, data->next);
   nfai_clear_state_set_captures(vm, data->current, 0);
   vm->captures = NULL;

   /* unmark all states */
   data->current->nstates = 0;
   data->next->nstates = 0;

   /* create a new empty capture set */
   set = NULL;
   if (vm->ncaptures) {
      set = nfai_make_capture_set(vm);
      if (!set) { NFAI_ASSERT(vm->error); return vm->error; }
      memset(set->capture, 0, vm->ncaptures * sizeof(NfaCapture));
   }

   /* mark entry state(s) */
   nfai_trace_state(vm, location, 0, set, context_flags);
   nfai_swap_state_sets(vm);

   return vm->error;
}

NFA_API int nfa_exec_step(NfaMachine *vm, char byte, int location, uint32_t context_flags) {
   struct NfaiMachineData *data;
#ifdef NFA_TRACE_MATCH
   char buf[8];
#endif
   int i;
   NFAI_ASSERT(vm);
   if (vm->error) { return vm->error; }
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;

#ifdef NFA_TRACE_MATCH
   fprintf(stderr, "[%2d] %s\n", location, nfai_quoted_char((uint8_t)byte, buf, sizeof(buf)));
#endif

   for (i = 0; i < data->current->nstates; ++i) {
      struct NfaiCaptureSet *set;
      int istate, inextstate, follow;
      uint16_t op, arg;

      istate = data->current->state[i];
      inextstate = istate + 1;
      NFAI_ASSERT(istate >= 0 && istate < vm->nfa->nops);

      set = (data->current->captures ? data->current->captures[istate] : NULL);
      op = vm->nfa->ops[istate] & NFAI_OPCODE_MASK;
      arg = NFAI_LO_BYTE(vm->nfa->ops[istate]);

      /* ignore transition ops */
      if (op == NFAI_OP_JUMP ||
            op == NFAI_OP_ASSERT_CONTEXT ||
            op == NFAI_OP_SAVE_START ||
            op == NFAI_OP_SAVE_END) {
         continue;
      }

#ifdef NFA_TRACE_MATCH
      nfai_print_opcode(vm->nfa, istate, stderr);
#endif

      follow = 0;

      switch (op) {
         case NFAI_OP_MATCH_ANY:
            follow = 1;
            break;
         case NFAI_OP_MATCH_BYTE:
            follow = (arg == (uint8_t)byte);
            break;
         case NFAI_OP_MATCH_BYTE_CI:
            NFAI_ASSERT(nfai_is_ascii_alpha_lower(arg));
            follow = (arg == nfai_ascii_tolower((uint8_t)byte));
            break;
         case NFAI_OP_MATCH_CLASS:
            {
               int j;
               for (j = 1; j <= arg; ++j) {
                  uint8_t first = NFAI_HI_BYTE(vm->nfa->ops[istate + j]);
                  uint8_t last = NFAI_LO_BYTE(vm->nfa->ops[istate + j]);
                  if ((uint8_t)byte < first) { break; }
                  if ((uint8_t)byte <= last) {
                     follow = 1;
                     break;
                  }
               }
               inextstate = istate + 1 + arg;
            }
            break;
         case NFAI_OP_ACCEPT:
            /* accept state is sticky */
            nfai_trace_state(vm, location + 1, istate, set, context_flags);
            if (vm->error) { return vm->error; }
            /* don't try any lower priority alternatives */
            ++i;
            if (data->current->captures) { data->current->captures[istate] = NULL; }
            goto break_for;
         default:
            NFAI_ASSERT(0 && "invalid operation");
            break;
      }

      if (follow) {
         nfai_trace_state(vm, location + 1, inextstate, set, context_flags);
         if (vm->error) { return vm->error; }
      } else {
         if (set) { nfai_decref_capture_set(vm, set); }
      }
      if (data->current->captures) { data->current->captures[istate] = NULL; }
   }
break_for:

   if (data->current->captures) {
      nfai_clear_state_set_captures(vm, data->current, i);
      nfai_assert_no_captures(vm, data->current);
   }

   data->current->nstates = 0;
   nfai_swap_state_sets(vm);
   NFAI_ASSERT(!vm->error);
   return 0;
}

NFA_API int nfa_exec_match_string(NfaMachine *vm, const char *text, size_t length) {
#ifdef NFA_TRACE_MATCH
   struct NfaiMachineData *data;
#endif
   uint32_t flags;
   const size_t NULL_LEN = (size_t)(-1);

   NFAI_ASSERT(vm);
   NFAI_ASSERT(text);

   if (vm->error) { return vm->error; }

   flags = NFA_EXEC_AT_START;
   if ((length == 0u) || (length == NULL_LEN && text[0] == '\0')) {
      flags |= NFA_EXEC_AT_END;
   }

   nfa_exec_start(vm, 0, flags);
   if (vm->error) { return vm->error; }

#ifdef NFA_TRACE_MATCH
   NFAI_ASSERT(vm->data);
   data = (struct NfaiMachineData*)vm->data;
#endif

   if (length || text[0]) {
      int at_end;
      size_t i = 0;
      do {
         char c = text[i];
         ++i;
         at_end = ((length == NULL_LEN) ? (text[i] == '\0') : (length == i));
         nfa_exec_step(vm, c, i - 1, (at_end ? NFA_EXEC_AT_END : 0));
         if (vm->error) { return vm->error; }
#ifdef NFA_TRACE_MATCH
         if (ncaptures) { nfai_print_captures(stderr, vm, data->current); }
#endif
      } while (!at_end && !nfa_exec_is_rejected(vm));
   }

#ifdef NFA_TRACE_MATCH
   if (ncaptures) {
      fprintf(stderr, "final captures (current):\n");
      nfai_print_captures(stderr, vm, data->current);
      fprintf(stderr, "final captures (next):\n");
      nfai_print_captures(stderr, vm, data->next);
   }
#endif

   if (vm->error) { return vm->error; }

   return nfa_exec_is_accepted(vm);
}

NFA_API int nfa_match(const Nfa *nfa, NfaCapture *captures, int ncaptures, const char *text, size_t length) {
   NfaMachine vm;
   int accepted;

   NFAI_ASSERT(nfa);
   NFAI_ASSERT(ncaptures >= 0);
   NFAI_ASSERT(captures || !ncaptures);
   NFAI_ASSERT(text);
   NFAI_ASSERT(nfa->nops >= 1);

   nfa_exec_init(&vm, nfa, ncaptures);

   accepted = nfa_exec_match_string(&vm, text, length);

   if (accepted >= 0) {
      if (ncaptures) {
         nfai_store_captures(&vm, captures, ncaptures);
      }
      NFAI_ASSERT(!vm.error);
   }

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

NFA_API int nfa_builder_init(NfaBuilder *builder) {
   NFAI_ASSERT(builder);
   builder->data = NULL;
   builder->error = nfai_alloc_init_default(&builder->alloc);
   return nfai_builder_init_internal(builder);
}

NFA_API int nfa_builder_init_pool(NfaBuilder *builder, void *pool, size_t pool_size) {
   NFAI_ASSERT(builder);
   builder->data = NULL;
   builder->error = nfai_alloc_init_pool(&builder->alloc, pool, pool_size);
   return nfai_builder_init_internal(builder);
}

NFA_API int nfa_builder_init_custom(NfaBuilder *builder, NfaPageAllocFn allocf, void *userdata) {
   NFAI_ASSERT(builder);
   builder->data = NULL;
   builder->error = nfai_alloc_init_custom(&builder->alloc, allocf, userdata);
   return nfai_builder_init_internal(builder);
}

NFA_API void nfa_builder_free(NfaBuilder *builder) {
   if (!builder) { return; }
   if (!builder->alloc.allocf) { return; }
   nfai_free_pool(&builder->alloc);
   memset(builder, 0, sizeof(NfaBuilder));
}

NFA_API Nfa *nfa_builder_output(NfaBuilder *builder) {
   Nfa *nfa;
   size_t sz;
   NFAI_ASSERT(builder);
   if (builder->error) { return NULL; }
   sz = nfa_builder_output_size(builder);
   if (!sz) { NFAI_ASSERT(builder->error); return NULL; }
   nfa = (Nfa*)malloc(sz);
   if (!nfa) { builder->error = NFA_ERROR_OUT_OF_MEMORY; }
   else { nfa_builder_output_to_buffer(builder, nfa, sz); }
   return nfa;
}

NFA_API size_t nfa_builder_output_size(NfaBuilder *builder) {
   struct NfaiBuilderData *data;
   int nops;

   NFAI_ASSERT(builder);
   if (builder->error) { return 0u; }
   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;
   if (data->nstack == 0) { builder->error = NFA_ERROR_STACK_UNDERFLOW; return 0; }
   if (data->nstack > 1) { builder->error = NFA_ERROR_UNCLOSED; return 0; }
   NFAI_ASSERT(data->stack[0]);
   NFAI_ASSERT(data->frag_size[0] >= 0);
   nops = data->frag_size[0] + 1; /* +1 for the NFAI_OP_ACCEPT at the end */
   return (sizeof(Nfa) + (nops - 1)*sizeof(NfaOpcode));
}

NFA_API int nfa_builder_output_to_buffer(NfaBuilder *builder, Nfa *nfa, size_t size) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *frag, *first;
   int to, nops;
   size_t required_size;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack == 0) { return (builder->error = NFA_ERROR_STACK_UNDERFLOW); }
   if (data->nstack > 1) { return (builder->error = NFA_ERROR_UNCLOSED); }

   NFAI_ASSERT(data->stack[0]);
   NFAI_ASSERT(data->frag_size[0] >= 0);

   nops = data->frag_size[0] + 1; /* +1 for the NFAI_OP_ACCEPT at the end */
   required_size = (sizeof(Nfa) + (nops - 1)*sizeof(NfaOpcode));
   if (size < required_size) { return (builder->error = NFA_ERROR_BUFFER_TOO_SMALL); }

   first = frag = data->stack[0];
   to = 0;
   do {
      memcpy(nfa->ops + to, frag->ops, frag->nops * sizeof(NfaOpcode));
      to += frag->nops;
      frag = frag->next;
   } while (frag != first);
   nfa->ops[to++] = NFAI_OP_ACCEPT;
   nfa->nops = to;
   NFAI_ASSERT(nfa->nops == nops);
   return 0;
}

NFA_API int nfa_build_match_empty(NfaBuilder *builder) {
   nfai_push_new_fragment(builder, 0);
   return builder->error;
}

NFA_API int nfa_build_match_string(NfaBuilder *builder, const char *bytes, size_t length, int flags) {
   struct NfaiFragment *frag;

   NFAI_ASSERT(builder);

   if (builder->error) { return builder->error; }
   if (length == (size_t)(-1)) { length = strlen(bytes); }
   if (length > NFA_MAX_OPS) { return (builder->error = NFA_ERROR_NFA_TOO_LARGE); }

   frag = nfai_push_new_fragment(builder, length ? length : 1);
   if (!frag) { return builder->error; }

   if (length) {
      int i;
      for (i = 0; i < (int)length; ++i) {
         frag->ops[i] = nfai_byte_match_op(bytes[i], flags);
      }
   } else {
      frag->ops[0] = NFAI_OP_NOP;
   }
   return 0;
}

NFA_API int nfa_build_match_byte(NfaBuilder *builder, char c, int flags) {
   return nfai_push_single_op(builder, nfai_byte_match_op(c, flags));
}

NFA_API int nfa_build_match_byte_range(NfaBuilder *builder, char first, char last, int flags) {
   const uint8_t a = first, b = last;
   struct NfaiFragment *frag;

   NFAI_ASSERT(builder);
   NFAI_ASSERT(a <= b);

   if (builder->error) { return builder->error; }

   if (flags == 0) {
      frag = nfai_push_new_fragment(builder, 2);
      if (!frag) { return builder->error; }
      frag->ops[0] = NFAI_OP_MATCH_CLASS | 1u;
      frag->ops[1] = (a << 8) | b;
   } else if (flags & NFA_MATCH_CASE_INSENSITIVE) {
      /* in the worst case, the input range contains the end of the upper-case range
       * through to the beginning of the lower-case range. In that situation, you can
       * end up with three ranges, e.g., [X-c] needs to turn into [A-C], [X-c], [x-z]
       */
      const uint8_t ascii_a = 97, ascii_z = 122, ascii_A = 65, ascii_Z = 90;
      uint16_t r0, r1, r2;
      int i, nranges;
      r0 = r1 = r2 = ((a << 8) | b);

      /* if range includes lowercase, mirror that segment to uppercase */
      if (a <= ascii_z && b >= ascii_a) {
         uint8_t a1 = (a < ascii_a ? ascii_a : a) - (ascii_a - ascii_A);
         uint8_t b1 = (b > ascii_z ? ascii_z : b) - (ascii_a - ascii_A);
         NFAI_ASSERT(a1 <= b1);
         NFAI_ASSERT(b1 <= b);
         if (a1 < a) { /* new range is not wholly contained in original range */
            if (b1 + 1 >= a) {
               /* ranges touch or overlap */
               r0 = r1 = r2 = (a1 << 8) | b;
            } else {
               /* new range is separate from original range */
               r0 = (a1 << 8) | b1;
            }
         }
      }

      /* if range includes uppercase, mirror that segment to lowercase */
      if (a <= ascii_Z && b >= ascii_A) {
         uint8_t a1 = (a < ascii_A ? ascii_A : a) + (ascii_a - ascii_A);
         uint8_t b1 = (b > ascii_Z ? ascii_Z : b) + (ascii_a - ascii_A);
         NFAI_ASSERT(a1 <= b1);
         NFAI_ASSERT(a1 >= a);

         if (b1 > b) { /* new range is not wholly contained in original range */
            if (b + 1 >= a1) {
               /* ranges touch or overlap */
               if (r0 == r1) {
                  r0 = r1 = r2 = (r1 & 0xFF00u) | b1;
               } else {
                  r1 = r2 = (r1 & 0xFF00u) | b1;
               }
            } else {
               /* new range is separate from original range */
               r2 = (a1 << 8) | b1;
            }
         }
      }

      nranges = 1;
      if (r0 != r1) { ++nranges; }
      if (r1 != r2) { ++nranges; }
      frag = nfai_push_new_fragment(builder, 1 + nranges);
      if (!frag) { return builder->error; }
      frag->ops[0] = NFAI_OP_MATCH_CLASS | nranges;
      frag->ops[i = 1] = r0;
      if (r0 != r1) { frag->ops[++i] = r1; }
      if (r1 != r2) { frag->ops[++i] = r2; }
   }
   return 0;
}

NFA_API int nfa_build_match_any(NfaBuilder *builder) {
   return nfai_push_single_op(builder, NFAI_OP_MATCH_ANY);
}

NFA_API int nfa_build_join(NfaBuilder *builder) {
   struct NfaiBuilderData *data;
   int i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 2) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 2;

   /* link and put on stack */
   data->stack[i] = nfai_link_fragments(data->stack[i], data->stack[i+1]);
   data->frag_size[i] += data->frag_size[i+1];

   /* pop stack */
   data->stack[i+1] = NULL;
   data->frag_size[i+1] = 0;
   --data->nstack;
   return 0;
}

NFA_API int nfa_build_alt(NfaBuilder *builder) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *frag = NULL;
   int frag_size = 0, i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 2) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 2;
   nfai_make_alt(builder,
         data->stack[i], data->frag_size[i],
         data->stack[i+1], data->frag_size[i+1],
         &frag, &frag_size);
   if (builder->error) { return builder->error; }

   data->stack[i] = frag;
   data->frag_size[i] = frag_size;
   data->stack[i+1] = NULL;
   data->frag_size[i+1] = 0;
   --data->nstack;
   return 0;
}

NFA_API int nfa_build_zero_or_one(NfaBuilder *builder, int flags) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *frag = NULL;
   int frag_size = 0, i;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 1;
   if (data->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (flags & NFA_REPEAT_NON_GREEDY) {
      nfai_make_alt(builder,
            (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT, 0,
            data->stack[i], data->frag_size[i],
            &frag, &frag_size);
   } else {
      nfai_make_alt(builder,
            data->stack[i], data->frag_size[i],
            (struct NfaiFragment*)&NFAI_EMPTY_FRAGMENT, 0,
            &frag, &frag_size);
   }
   if (builder->error) { return builder->error; }

   data->stack[i] = frag;
   data->frag_size[i] = frag_size;
   return 0;
}

NFA_API int nfa_build_zero_or_more(NfaBuilder *builder, int flags) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *fork, *jump, *frag;
   int i, x;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 1;

   if (data->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (data->frag_size[i] + 5 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }
   jump = nfai_new_fragment(builder, 2);
   if (!jump) { return builder->error; }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_JUMP | (uint8_t)2;
   fork->ops[1] = fork->ops[2] = 0;
   x = ((flags & NFA_REPEAT_NON_GREEDY) ? 1 : 2);
   fork->ops[x] = data->frag_size[i] + 2;

   jump->ops[0] = NFAI_OP_JUMP | (uint8_t)1;
   jump->ops[1] = -(data->frag_size[i] + 5);

   /* link and put on stack */
   frag = nfai_link_fragments(fork, data->stack[i]);
   frag = nfai_link_fragments(frag, jump);
   data->stack[i] = frag;
   data->frag_size[i] += jump->nops + fork->nops;
   return 0;
}

NFA_API int nfa_build_one_or_more(NfaBuilder *builder, int flags) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *fork;
   int i, x;

   NFAI_ASSERT(builder);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 1;

   if (data->stack[i] == &NFAI_EMPTY_FRAGMENT) {
      return (builder->error = NFA_ERROR_REPETITION_OF_EMPTY_NFA);
   }

   if (data->frag_size[i] + 3 > NFAI_MAX_JUMP) {
      return (builder->error = NFA_ERROR_NFA_TOO_LARGE);
   }

   fork = nfai_new_fragment(builder, 3);
   if (!fork) { return builder->error; }

   /* fill in fragments */
   fork->ops[0] = NFAI_OP_JUMP | (uint8_t)2;
   fork->ops[1] = fork->ops[2] = 0;
   x = ((flags & NFA_REPEAT_NON_GREEDY) ? 2 : 1);
   fork->ops[x] = -(data->frag_size[i] + 3);

   /* link and put on stack */
   data->stack[i] = nfai_link_fragments(data->stack[i], fork);
   data->frag_size[i] += fork->nops;
   return 0;
}

NFA_API int nfa_build_capture(NfaBuilder *builder, int id) {
   struct NfaiBuilderData *data;
   struct NfaiFragment *start, *end, *frag;
   int i;

   NFAI_ASSERT(builder);
   NFAI_ASSERT(id >= 0);
   NFAI_ASSERT(id <= UINT8_MAX);
   if (builder->error) { return builder->error; }

   NFAI_ASSERT(builder->data);
   data = (struct NfaiBuilderData*)builder->data;

   if (data->nstack < 1) {
      return (builder->error = NFA_ERROR_STACK_UNDERFLOW);
   }

   i = data->nstack - 1;

   start = nfai_new_fragment(builder, 1);
   if (!start) { return builder->error; }
   end = nfai_new_fragment(builder, 1);
   if (!end) { return builder->error; }

   /* fill in fragments */
   start->ops[0] = NFAI_OP_SAVE_START | (uint8_t)id;
   end->ops[0]   = NFAI_OP_SAVE_END   | (uint8_t)id;

   /* link and put on stack */
   frag = nfai_link_fragments(start, data->stack[i]);
   data->stack[i] = nfai_link_fragments(frag, end);
   data->frag_size[i] += start->nops + end->nops;
   return 0;
}

NFA_API int nfa_build_assert_at_start(NfaBuilder *builder) {
   return nfa_build_assert_context(builder, NFA_EXEC_AT_START);
}

NFA_API int nfa_build_assert_at_end(NfaBuilder *builder) {
   return nfa_build_assert_context(builder, NFA_EXEC_AT_END);
}

NFA_API int nfa_build_assert_context(NfaBuilder *builder, uint32_t flag) {
   int i;
   NFAI_ASSERT(flag);
   NFAI_ASSERT((flag & (flag - 1)) == 0);
   for (i = 0; i < 32; ++i) {
      if (flag & (1u << i)) { break; }
   }
   NFAI_ASSERT(i >= 0 && i < 32);
   return nfai_push_single_op(builder, NFAI_OP_ASSERT_CONTEXT | (uint8_t)i);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

/* vim: set ts=8 sts=3 sw=3 et: */
