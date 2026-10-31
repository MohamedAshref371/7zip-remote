# Renewing an archive URL

File Manager exposes **File → Renew archive URL...** for remote sessions. It
accepts a new direct HTTP(S) URL without saving either URL in combo history.
The command preserves archive type, ETag policy, cache directory and quota.

For changed URLs, previous unverified-reuse preferences do not authorize linking
the previous cache. The user chooses Yes (fresh metadata/sample verification),
No (do not link the previous entry), or Cancel (retain the session). Matching
samples are not full-file verification. Changed content detected by existing
cache checks starts an isolated cache generation. ETag bypass remains independent.

The new source must open successfully before replacing the previous archive.
The existing cache handoff allows reuse without competing with the old session's
lock. Failure/cancellation restores the old cache when its generation is intact;
otherwise its stream retains memory/network fallback. This preserves browsing,
but cannot restore access through an expired original URL.

Refresh and renewal share the reopen path. It restores the current subfolder,
flat mode and selected names where possible. Missing subfolders fall back to the
new archive root. Renewal from a nested archive returns to the outer archive;
failure retains the previous nested view.

After extraction/testing encounters HTTP 403, 404 or 410, File Manager offers
renewal. Failed non-canceled Refresh also offers it. The offer waits for the
panel's timer/notification guards to be restored and its parent window to be
enabled, so an error dialog's nested message loop cannot replace an archive
still being used by the extraction caller. Selection is saved before callers
clear it. Pending offers are discarded if the URL or view path has changed.

There is no automatic extraction restart or decompressor-state resume. Run
Extract/Test again after renewal; existing overwrite prompts and policies apply.

## Validation

Build: `.build/msvc/renew-url/`; local-only build: `.build/msvc/renew-url-local/`.

The 15 renewal scenarios exercise ZIP, 7z, encrypted 7z, expired 403/404/410,
independent linking consent, no-cache operation, changed-content isolation,
ETag bypass, cancellation, invalid/denied new links, and retained browsing when
both links fail. Successful output is compared byte-for-byte against its source.
The existing 14 cache-Agent and 60 Agent scenarios also pass.

These are headless Agent/backend tests. Desktop menu interaction, restoration
of visible selections and the deferred offer still require manual acceptance;
the Windows automation runtime could not start in this session.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/renew-url
python Tests/RemoteArchive/test_renewal.py --exe .build/msvc/renew-url/agentcheck.exe
```
