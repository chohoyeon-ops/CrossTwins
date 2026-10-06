# Phase 2B navigation regression tests

CMake extracts the current production `navigateButtons()` methods and Home
cover-grid callback block. Small adapters replace input, list state, rendering
requests and activity transitions; the production `ButtonNavigator` implementation
is compiled unchanged. These tests run in the normal host suite.

Coverage includes release-only list movement, wrap boundaries, rapid navigation,
hold/repeat release suppression, library tab/search versus row paging, Home
cover/menu bands, empty lists and activity destruction/recreation. Display tasks
and actual hardware input still require device smoke tests.

The capture optimization depends on `ButtonNavigator` consuming callbacks
synchronously without retaining them. Stack snapshots preserve the original
count and bound navigation object, including across tab changes within a helper
call. A future asynchronous callback API would need owned captures instead.
