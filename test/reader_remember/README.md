# Successful-render registration tests

The shared entry/exit/registration code, TXT renderBook, XTC renderPage and
EPUB progress restoration/end-view/success publication blocks are extracted from
production sources. Rendering, storage failures and format initialization are
host adapters. XTC page-buffer allocation is injected with a test-only malloc
macro. Existing progress readers/serializers and the on-demand TXT suite also
run in the full host suite.

Tests cover new/existing records for all three formats, simulated missing or
invalid load and cancellation, TXT read/non-advancing failures, XTC allocation
and read failures, EPUB/XTC progress restoration, empty TXT, zero-spine EPUB,
cross-thread publication and exit before the next reader loop.

These are not complete EPUB/XTC activity tests with real displays or SD cards.
Corrupt ZIP/section fixtures through the full activity, hardware Back/Home
cancellation and sleep/wake timing still require device validation. Existing
Recent Books implementations and progress formats are not replaced by adapters
in production.
