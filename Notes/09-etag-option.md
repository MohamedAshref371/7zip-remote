# Optional ETag Verification Bypass

**Later UI update:** at the user's request, the Open URL dialog now defaults to
ETag bypass enabled and includes an archive type dropdown. Transport/CLI defaults
remain strict. See [Open URL options](10-open-url-options.md). The text below
records the initial bypass implementation and its earlier checkbox default.

The user requested an option to read servers without a strong ETag, accepting
that an archive is unlikely to change during a session. This change makes that
tradeoff explicit and leaves the default consistency policy intact.

## Behavior

- Strict mode remains the default: every response needs a strong ETag, and
  subsequent requests carry `If-Match` with the initial value.
- **File Manager:** File → Open URL... now includes **Skip ETag verification**,
  unchecked by default. The selected policy belongs to the current session,
  survives Refresh, and is shown as `[HTTP; ETag off]` in the panel path.
- **Console:** `-shttp-no-etag` opts in for `l`, `x`, `e`, and `t` with a direct
  HTTP/HTTPS URL. Using the flag with a local path is rejected. A diagnostic
  identifies that ETag verification is disabled.
- Bypass mode neither reads/verifies ETags nor sends `If-Match`; absent, weak,
  duplicate, and changed ETags are ignored.
- Total size stays pinned during each open session. Content-Range, body length,
  response status, content representation, and normal TLS validation remain
  mandatory. The archive handlers' existing integrity checks are unchanged.
- A same-size change may go undetected and can cause incorrect results or an
  extraction error. The dialog explains this consequence. This mode does not
  establish snapshot consistency and is not enabled automatically for a host.
- Cache remains per session. Refresh opens a fresh source with the same policy;
  no setting is persisted to registry or silently applied to future dialogs.

## Implementation

`CHttpRangeOptions::SkipETagVerification` controls validation and conditional
headers in the HTTP transport. The CLI passes its parsed option to listing and
extraction. The GUI passes the checkbox through its opening worker, folder
manager, and Agent, and stores the setting in the panel's remote folder link
for refresh. Read-only enforcement and zone metadata remain active.

## Validation

- **85 CLI scenarios passed**, including the original strict-mode tests, bypass
  listing/test/extraction, omitted If-Match, continued rejection of malformed
  ranges and changed sizes, and strict defaults on a subsequent new invocation.
- **29 Agent scenarios passed**, including bypass through the File Manager's
  backend, encrypted headers, callback lifetime, cancellation before open,
  read-only mutation rejection, and continued size-change rejection.
- **72 prototype/stream scenarios passed** with verification enabled by default,
  including 64-bit offsets, network failures, timeout, and cancellation.
- The user's real MediaFire HTTPS URL rejected strict mode because it returned
  no ETag. With bypass enabled, the new console listed the archive and extracted
  a selected 589,824-byte member successfully. Its SHA-256 matched the same member
  in `Tests/tafasir.zip`. The full URL is not stored in repository reports.
- The local reference archive has SHA-256
  `bbd81002444b1e9b90b23205d54670f2d9a53d509769b24150f1c690f3a2399d`
  and size 35,026,188 bytes. It is a user-provided fixture, not a required checked-in
  dependency or a file executed during testing.
- Retained summary: [etag-option-results.json](etag-option-results.json).
- Console and File Manager builds without HTTP also succeeded; the plain console
  passed a full integrity test of `Tests/tafasir.zip`. Resource inspection of the
  delivered GUI executable confirmed the new dialog and checkbox are included.

The File Manager resource and code compile with MSVC. The computer-use native
pipe is still unavailable, so the new checkbox's desktop interaction and the
Refresh flow must be manually verified. Agent tests do not establish those UI
interactions. Previous user feedback confirmed the earlier Open URL flow worked.

## Try the Build

The new binaries are collected separately in `.build/msvc/etag-option/`, allowing
an earlier File Manager build to remain open. Keep the EXE and `7z.dll` together.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/etag-option
& .\.build\msvc\etag-option\7zFM.exe

& .\.build\msvc\etag-option\7z.exe l 'https://example.com/archive.zip' -shttp-no-etag
```

The URL above is a placeholder. For the GUI, enable **Skip ETag verification**
only for the intended remote session. Redirects, remote volumes, browser login,
and full-download fallback remain unsupported.
