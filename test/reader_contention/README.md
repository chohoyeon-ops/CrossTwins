# EPUB contention tests

Tests extract the production RenderLock implementation, background build tick,
shared scheduling predicate and main-loop hint query. A host mutex models the
existing non-recursive FreeRTOS rendering mutex; no new production mutex is used.

Coverage includes immediate try-lock failure without section access, contention
backoff, failed-build cleanup, heap admission, concurrent section replacement,
rapid scheduling queries, compatibility construction and explicit unlock.
Real FreeRTOS scheduling, display callbacks, page-turn latency, sleep/wake and
activity stack transitions still require device tests. This does not claim that
every existing EPUB input path is race-free.
