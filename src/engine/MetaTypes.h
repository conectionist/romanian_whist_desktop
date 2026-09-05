#ifndef META_TYPES_H
#define META_TYPES_H

// Registers every payload that crosses the thread boundary as a queued signal
// argument. Call it once, EARLY - before anything connects.
//
// A function rather than a handful of lines in main(), because there are two
// binaries: the application and the test suite. The test suite links
// Catch2WithMain and so has no main() of ours to put them in, and registering
// only in the application means the tests run unregistered - which is precisely
// the configuration whose failure mode is a signal that silently never arrives.
// Tests would then pass, or fail, for reasons the shipped program does not
// share, which is the one thing a test suite must never do.
//
// Qt 6 builds a QMetaType from any complete type, so most of these resolve with
// nothing registered at all and omitting the call is not a compile error (see
// section 5, and Appendix A.3). It is done anyway: wherever auto-registration
// does not reach, the diagnostic is one line on stderr - "cannot queue argument
// of type X" - and a game that appears to hang with no crash to point at.
void registerMetaTypes();

#endif
