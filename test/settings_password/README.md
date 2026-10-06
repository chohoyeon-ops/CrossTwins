# Settings password exposure regressions

`SettingsPasswordTest` compiles the actual `SettingInfo`, KOReader metadata,
settings GET/POST handlers, and internal auth-header helper extracted at CMake
configure time. It uses ArduinoJson 7.4.2 and small transport/credential-store
adapters. These tests cover the serialization and setter boundaries; they do
not perform live HTTP/TLS requests or physical SD persistence. The auth adapter
records the input to Base64 encoding rather than testing the encoding algorithm.

`SettingsPasswordBrowser.test.cjs` runs production functions from the HTML
template in Node with a small DOM/fetch adapter. It covers redacted rendering,
unchanged/set/clear requests, and cleanup after successful POST, including a
failed subsequent reload. A failed POST preserves the user's new input for retry.
It does not simulate browser layout, autofill, network access controls, or TLS.

Run through CTest or directly with:

```powershell
node --test test/settings_password/SettingsPasswordBrowser.test.cjs
```

On-device checks: start File Transfer in LAN and AP modes, inspect
`GET /api/settings` for `koPassword: value=null` and correct `hasPassword`, then
verify unrelated edits, setting/clearing a password, and KOReader authentication.

This fix does not add HTTP authentication. The existing file-transfer server is
available to clients that can reach it while it is running; the default AP is
open. Credential persistence remains in the existing credential store format.
