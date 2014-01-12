# libnfa

A regular expression library for situations where you don't want regular
expression syntax.

## Compilation and Embedding

libnfa can be compiled as C (C89 or greater) or C++ (C++98 or greater), and
should not produce any warnings. All identifiers are prefixed to avoid
naming conflicts, and this includes internal identifiers used in `nfa.c`.

Some preprocessor definitions control compilation options:

* `NFA_API` can be defined to apply a linking attribute to public libnfa
  symbols. For example, if you are embedding libnfa into one of your own
  translation units, you may wish to `#define NFA_API static` to prevent
  symbols leaking out of that translation unit.

* `NFA_NO_STDIO` can be defined to exclude the (few) functions that use
  stdio functionality.

* `NDEBUG` or `NFA_NDEBUG` can be defined to disable assertions
  (note: assertions are used to check static conditions on function
  arguments, so it is advisable to leave them enabled during development).

## API Overview

The libnfa API is split into two parts: building an NFA, and executing
an NFA.

### The `Nfa` Object

A compiled NFA is represented as an `Nfa` object. This object contains only
static data, it is stored in a single block of memory, and it does not
contain any pointers. The internals of an `Nfa` object may change in future
versions of libnfa, but for a particular libnfa version, an `Nfa` object can
be written to disk as a data blob, or serialised in some other way.

An `Nfa` object is constructed using an `NfaBuilder`. It can then be used to
match an input string using an `NfaMachine`. A single `Nfa` object can be
used by multiple `NfaMachine`s simultaneously.

### Expression Building

An `NfaBuilder` holds a stack of expressions. This behaves similarly to a
reverse polish notation calculator: basic expressions are pushed onto
the stack, and derived expressions (e.g., alternation, repetition, etc)
are built by popping their arguments and pushing the result.

The `NfaBuilder` structure itself is not allocated by libnfa. Typically,
you would use an `NfaBuilder` object on the C stack, or embed it in one of
your own structures. The `NfaBuilder` object must be initialised with
one of the `nfa_builder_init*` functions, and when you're finished with it
you must call `nfa_builder_free` to release attached resources.

When you have constructed the desired expression, you can call the
`nfa_builder_output` function to compile the final expression to an `Nfa`
object.

The `Nfa` object returned by `nfa_builder_output` is allocated with
`malloc` and should be freed by the user with `free`. If you want to use
a custom allocation method, you should call `nfa_builder_output_size` to
find the required size (in bytes) of the `Nfa` object, and then call
`nfa_builder_output_to_buffer` to write the compiled object into your own
memory buffer.

Example:

    /* builds the expression 'foo((?:bar|qux)+)' */
    Nfa *build_nfa_example(void) {
       Nfa *nfa = NULL;
       NfaBuilder b; /* note: NfaBuilder allocated on stack */

       /* initialise builder */
       nfa_builder_init(&b);

       /* build expression                         EXPRESSION STACK  */
       nfa_build_match_string(&b, "foo", 3, 0); /* foo               */
       nfa_build_match_string(&b, "bar", 3, 0); /* foo, bar          */
       nfa_build_match_string(&b, "qux", 3, 0); /* foo, bar, qux     */
       nfa_build_alt(&b);                       /* foo, bar|qux      */
       nfa_build_one_or_more(&b, 0);            /* foo, (bar|qux)+   */
       nfa_build_capture(&b, 0);                /* foo, ((bar|qux)+) */
       nfa_build_join(&b);                      /* foo((bar|qux)+)   */

       /* compile the expression to an NFA */
       nfa = nfa_builder_output(&b);

       /* check for any errors that might have happened */
       if (!nfa) {
          fprintf(stderr, "error building NFA: %s\n",
             nfa_error_string(b.error));
       }

       /* release builder resources */
       nfa_builder_free(&b);
       return nfa;
    }

See the API Reference for details of the expression stack operations.

### Execution

#### Simple Matching

If you just want to execute an NFA on an input string and do not need
special context flags (for assertions -- described below) or custom memory
management, then you can use the `nfa_match` function.

Example:

    void match_example(const char *input) {
       NfaCapture groups[1];
       int ret;
       Nfa *nfa = build_nfa_example();
       if (!nfa) { return; }

       ret = nfa_match(nfa, groups, 1, input, -1);
       free(nfa);

       if (ret < 0) {
          fprintf(stderr, "error executing NFA: %s\n",
             nfa_error_string(ret));
          return;
       }

       if (ret == NFA_RESULT_MATCH) {
          fprintf(stdout, "Match: '%s'\n", input);
          fprintf(stdout, "  capture: '%.*s'\n",
              groups[0].end - groups[0].begin, input + groups[0].begin);
       } else {
          fprintf(stdout, "No match for '%s'\n", input);
       }
    }

#### Execution

An `NfaMachine` object manages the execution state of an NFA.

*To be written...*

### Error Handling

`NfaBuilder` and `NfaMachine` objects each have an `error` field which holds
the current error state of the object. When an operation results in an
error, the error code is stored in this field. When a function is called
on an object that is in an error state (ie, with `error != NFA_NO_ERROR`),
the operation immediately fails, and the error state is left unchanged.
The exception to this is the `nfa_builder_free` and `nfa_exec_free`
functions, which free the object regardless of its error state.

Because errors are 'sticky', you can defer error checking to a convenient
point in your code.

With a few exceptions (noted below), builder and execution API functions
return the object's error state. You can check the return value directly,
or check the object's error state after the call.

Exceptions:

* `nfa_builder_free` and `nfa_exec_free` return `void` (they cannot fail).
* `nfa_print_machine` currently returns void because it is a debugging API
  and does not yet have any internal error checking.
* `nfa_builder_output` returns an `Nfa*` which is `NULL` on error.
* `nfa_builder_output_size` returns the size of the output `Nfa` in bytes,
  or 0 on error.
* `nfa_match` returns `NFA_RESULT_NOMATCH` (0), `NFA_RESULT_MATCH` (> 0) or
  an error code (< 0).
* `nfa_exec_is_accepted`, `nfa_exec_is_rejected` and `nfa_exec_is_finished`
  are predicates. They never change the `NfaMachine`'s error state.
  If the machine is already in an error state, then `accepted` is 0,
  `rejected` is 1 and `finished` is 1.

Most functions have some static requirements on their parameters. These
requirements are documented in the API Reference, and are checked by
assertions; they do not have associated error codes, and are not checked
at all if `NDEBUG` or `NFA_NDEBUG` is defined.

### Memory Management

`NfaBuilder` and `NfaMachine` each need to allocate memory during their
execution: `NfaBuilder` needs to allocate small blocks of memory for most
expression building operations. `NfaMachine` allocates blocks of memory
proportional to the `Nfa` size during initialisation, and if sub-match
captures are being used it also allocates blocks of memory during matching
(each block being proportional in size to the number of capture groups).

These allocations are satisfied from a memory pool owned by the object.
That pool is itself allocated using one of three methods:

* The default C library allocator, `malloc` (see `nfa_builder_init` and
  `nfa_exec_init`).
* A fixed size block of memory given at object initialisation time
  (see `nfa_builder_init_pool` and `nfa_exec_init_pool`).
* A custom allocator function (see `nfa_builder_init_custom` and
  `nfa_exec_init_custom`).

The underlying allocator (`malloc`, or the custom allocator function) is
called whenever the pool needs to be expanded. If the allocator fails,
or if a fixed size pool is being used and does not have enough space, then
the `NfaBuilder` or `NfaMachine` object is put into an error state (see
'Error Handling').

#### Custom Allocator

To use a custom allocator, call `*_init_custom`, passing in a pointer to
your custom allocation function, and a `void*` userdata pointer. The
allocator function interface is:

    void *CustomAllocator(void *userdata, void *p, size_t *size);

The `userdata` value is the value passed into `*_init_custom`. The function
is called for both allocation and deallocation.

When called for allocation, `p` is `NULL`, `*size` is the *minimum* size
required, and must be set by the allocator to the *actual* size allocated.
Typically, the allocator should allocate a block of memory of size
`max(*size, K)` for some sensible block size `K` (e.g., 1024 bytes,
or `NFA_DEFAULT_PAGE_SIZE`).

When called for deallocation, `p` points to the block of memory to release,
and `size` is `NULL`.

### Thread Safety

A single `NfaBuilder` or `NfaMachine` must only be used from one thread at
a time. Separate `NfaBuilder` or `NfaMachine` objects may be accessed from
separate threads simultaneously (^). An `Nfa` object is immutable after its
construction, and so it may be shared between threads.

(^) If you are using the default allocator, then thread-safety of libnfa
relies on thread-safety of libc `malloc` and `free`.

## Reference

*To be written...*

## Licence

  Copyright (C) 2014 John Bartholomew

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not
     be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

<!-- vim: set ts=8 sts=2 sw=2 et colorcolumn=76: -->
