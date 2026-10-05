# Phase 2A focused regression tests

Registered in the standard host CMake suite. Configuration copies the current
production navigation routines, Recent Books add/prune routines, title
truncation routine and screenshot filename routine into a generated test TU.
It does not maintain separate copies of their algorithms. Production source
changes trigger CMake reconfiguration.

Adapters provide input events, file-existence results and a persistence-call
counter. Title tests use the real built-in Pretendard glyph metrics, kerning and
EpdFont measurement implementation. Filesystem sanitization and UTF-8 helpers
are compiled from their original production sources.

These tests do not emulate SD I/O, JSON persistence, the activity UI, bidi
reordering or SD font fallback. Those implementations remain unchanged by this
phase; hardware smoke tests should cover their integration.
