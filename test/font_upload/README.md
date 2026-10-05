# SD font upload short-write regressions

The target compiles the unchanged production font upload handlers/state and
FontInstaller validation, directory selection and path construction routines
against a fault-injecting storage adapter. It exercises the HTTP upload event
lifecycle and actual response/registry-dirty branches, not a standalone write
predicate. The adapter mirrors the inspected SDK's direct O_TRUNC open contract.

It covers complete/intermediate/final/zero writes, actual-byte accounting,
ignoring later data after failure, end/abort cleanup, a subsequent new upload,
existing-font truncation, both font roots, filename normalization and rejected
paths that must not redirect write or cleanup to credentials. Existing strict
font name policy is preserved; this phase does not add Unicode name support.

The payload fixture checks transport bytes and the existing magic-header gate,
not the runtime font parser. No runtime font loading/cache code is included.
Close failure and physical SD durability are not simulated. The current
handler ignores close/remove results; those semantics are unchanged. The removal
failure probe confirms a partial file can remain despite the failure response. Overwriting a
font still truncates the old file at start, and cannot restore it after failure.
This fix provides neither transactional replacement nor power-loss atomicity.

On device, verify a normal upload and usable SD font, insufficient-space/SD
write failure, the failure UI result, and discovery after reconnect/reload.
