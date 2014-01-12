--- libnfa ---

A regular expression library for situations where you don't want
regular expression syntax.

### todo

* Add error handling and reporting in nfa_exec_*
* Write documentation
* Do a code review / cleanup pass
* Determine a safe but not too loose upper bound on memory use for captures
* Write a test harness and test suite
* Add support for cloning a machine / saving and restoring machine state
* Add optimising transforms

### done

* Make everything work when ncaptures = 0
* Expose nfa_exec_* API
* Allow a custom allocator to be specified
