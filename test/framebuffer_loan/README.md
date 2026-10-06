# Framebuffer loan regression coverage

The fixture compiles the current GfxRenderer loan implementation, its nested RAII declaration, the SDK display lend/return implementation, and real BuildScratch registry. Hardware async completion is a synchronous counter stub. Tests cover nested inert loans, explicit end/destructor idempotence, exclusive scratch claims, stable allocation identity, white return, early failure and repeated operation scopes.

ReaderContention separately compiles the actual EPUB background tick/hint under a host mutex and checks stale-page redraw on successful/failed loans and suppression while an overlay is open. Existing InflateStream tests exercise real decompression using borrowed storage and failure cleanup.

This does not simulate the physical panel, FreeRTOS task scheduling, device sleep, or a full activity-manager transition. Parser fixtures do not decode real image entries. Validate image-heavy EPUBs under low heap, toolbar open/close during incremental builds, rapid chapter changes, and sleep/wake on X3 and X4 before device acceptance.
