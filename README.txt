--- libnfa ---

A regular expression library for situations where you don't want
regular expression syntax.

### Licence

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
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

### Version History

0.9  -- 6 February 2014
        First release.

### Todo

* Move the regex pattern parser into libnfa.
* Add API nfa_exec_match, implementing the core loop from nfa_match.
* Allow max stack to be overridden at compile time with a #define
  (instead of requiring an actual code change in nfa.h)
* Add some clear (supported) method of error recovery for some error conditions.
* Document recoverable vs. unrecoverable errors in the manual.
* Add support for cloning a machine, or saving and restoring machine state
* Add error checking to `nfa_print_machine`.

* Write API reference
* Write a test harness and test suite

* Do a code review / cleanup pass
* Clean up interface for capture indices somehow (track max index?)
* Determine a safe but not too loose upper bound on memory use for captures

### Done

* Make everything work when ncaptures = 0
* Expose nfa_exec_* API
* Allow a custom allocator to be specified
* Add error handling and reporting in nfa_exec_*
* Support pool and custom allocation for the NfaMachine object
* Support custom allocation for the Nfa object itself
* Write manual
* Make sure nfa_exec_start does something sensible if called during execution.
* Support case-insensitive range matches
* Merge alternated range / character matches into a single range match
* Add support for character classes in the regex parser.

Copyright © 2014 John Bartholomew
