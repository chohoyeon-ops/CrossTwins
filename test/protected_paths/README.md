# Credential path isolation tests

The header is tested directly. CMake also extracts unchanged production HTTP
download/upload/rename/move/mkdir/delete, WebSocket upload, and WebDAV decoded-path
and raw-upload routines, following the existing Phase 2 host adapter pattern.
Storage adapters record reads and mutations so rejected requests must fail before
opening, writing, renaming, creating, or removing files. These are persistence
boundary tests, not full HTTP server or FAT filesystem emulation.

Credential file prefixes include temporary/backup siblings. Protection accounts
for ASCII case, leading spaces, trailing dots/spaces, canonical parent traversal,
and shallow FAT 8.3 aliases. The alias policy is deliberately conservative: an
ordinary short-alias-looking name at the first two path levels is also denied
because HAL does not expose reliable alias-to-long-name resolution. Deeper aliases
remain usable. Valid Korean/UTF-8 filenames are preserved without Unicode
normalization; malformed UTF-8, embedded NUL and FAT-invalid separators/bytes fail
closed. Absolute SD paths remain supported and traversal is clamped at SD root;
individual new names still cannot contain traversal or separators.

Real-device checks should cover actual FAT long/short-name aliases, web file
listing/download/upload/rename/move/delete, WebDAV requests and aborted PUTs, and
normal Korean filenames. Credential store schemas and legitimate configuration
APIs are unchanged. Host adapters do not prove transport parser behavior or
physical FAT alias generation.
