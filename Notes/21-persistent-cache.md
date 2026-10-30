# Optional persistent archive cache

File Manager's **Open URL** dialog now has **Cache settings...**. Disk caching is
off by default. Settings let the user enable it, choose a writable directory,
set a positive limit in MiB (1 GiB initially), choose unlimited storage, inspect
usage, and clear stored data. Enable/path/limit settings persist under the current
user's `Software\7-Zip\FM\RemoteCache` key. Consent and the selected cache entry
are session choices and are never saved as global permission.

## Using it

1. Open **File → Open URL... → Cache settings...**.
2. Enable **Keep downloaded archive parts on disk**, choose a directory and limit,
   and save. Changes apply to newly opened archives.
3. For automatic verified reuse, uncheck **Skip ETag verification** when the
   server supports a strong ETag. The existing default of checking that bypass
   in each new Open URL dialog is retained.
4. For a changed URL, select the previous archive from **Saved archive cache**.
   Entries show a sanitized filename hint, source size, cache usage and local
   last-use date/time.
5. Linking another URL or reusing an old cache while skipping ETag requires the
   independent **Reuse old cache without full version verification** checkbox and
   an explicit confirmation. Skipping ETag alone does not grant this consent.

The remote status bar and its tooltip show whether the cache is new, verified,
partially verified by consent, ignored after source changes, busy, or unavailable.
Selection download estimates subtract indexed disk parts as well as memory
blocks. A corrupt part discovered later is fetched again, so those totals remain
approximate. Cache hits do not count as downloaded HTTP bytes.

## Identity and acceptance rules

Every session first performs the normal fresh, validated one-byte HTTP range
probe. Automatic reuse requires all of:

- An exact match of the original and resolved resource URL hashes.
- A matching total source size and matching strong ETag hash.
- An existing generation whose saved parts were acquired with ETag verification.

An ETag is not treated as a global content identifier. Different URLs, including
different signed query strings, are separate resources until explicitly linked.
The client does not remove query parameters to guess identity. A changed size or
a changed known strong ETag at the same resource rejects the old generation even
when partial reuse was requested.

New generations record a SHA-256 fingerprint of freshly fetched first/last
64 KiB edges. Explicitly unverified reuse checks those edges again and compares
up to three distinct stored parts (first, middle and last) with freshly fetched
bytes. These requests bypass both caches. No old bytes reach the archive engine
before acceptance. Any mismatch starts an isolated generation. A rejected link
does not overwrite the old parts or downgrade their previous verification state.

Matching samples **do not prove equality of the whole archive**. This mode can
miss changes outside the checked regions and can cause wrong output; the dialog
states this before consent. Generations reused this way remain unverified and
are never silently promoted to verified status.

If a redirect changes the resolved resource during cache acceptance, caching is
paused and the source uses memory. A later scope change while verified cached
bytes are already in use stops the read, quarantines the old generation from
automatic reuse, and requests reopening. This prevents mixing cached bytes from
one resource with another resource merely because their ETag strings coincide.
Initial normal redirect chains continue to resolve before cache matching.

## Storage and lifecycle

Only the application's `.7z-http-cache-v1` subdirectory is managed. The selected
parent directory and unrelated files are retained. A root ownership marker and
per-generation metadata/ownership markers identify the managed namespace. Cache
directories containing junctions/reparse points are refused, reparse entries are
not traversed, and unexpected names/subdirectories are not recursively removed.
An unrecognized or damaged root ownership marker is refused rather than adopted.

Complete, transport-validated 256 KiB parts are written to unique temporary files,
flushed and atomically renamed. Incomplete HTTP bodies never reach disk. Each
part carries its offset, size, SHA-256 payload digest, and a binding to the cache
generation and source identity. A valid part copied from another generation is
therefore rejected too. Metadata has its own checksum. Interrupted temporary
writes are ignored and cleaned under the entry lock; damaged parts are re-fetched.
Complete parts survive normal close or an abrupt process exit.

Full URLs, signed queries and HTTP credentials are not stored in plaintext in
metadata. Resource keys/validators are hashed; a sanitized filename hint is
retained for selection. Cached archive bytes are stored as received, not encrypted
by this feature. Checksums detect corruption; they are not authentication against
a process that can deliberately rewrite the user's cache files.

An exclusive per-entry file lock lasts for the source session. Another process
opening that same generation uses memory instead of sharing mutable state.
This can also happen during Refresh, which opens a new source while keeping the
old view alive until success. Session cache settings and manual selection are
retained. A short manager lock serializes catalog changes and pruning.

The limit counts logical sizes of recognized cache files, including part headers
and metadata; filesystem allocation can differ. Old eligible parts are pruned by
last use, and empty inactive entries can be removed. Foreign active entries stay
protected, so a newly reduced limit may remain exceeded until they close. Within
the owning session, disk parts can be evicted without losing data already in RAM.
Unlimited mode disables quota pruning. Limits are applied on opens and writes.

**Clear cache** skips active entries and reports their count; close those archives
and clear again to remove them. Unrecognized files are retained. If a part cannot
be deleted, ownership metadata is retained so a later clear can finish safely.
Cache read/write failure falls back to network/memory for the affected data and
sets a visible cache status; it does not turn a successful transfer into a failed
extraction. Network failures, cancellation and source identity changes retain
their normal failure behavior. Filesystem I/O latency is not a hard real-time
cancellation guarantee.

This persists transport parts, not decompressor state or a partially extracted
output file. A repeated extraction starts normally and reuses accepted parts.
The main console still uses its existing memory-cache defaults; GUI settings do
not silently enable persistent caching in console operations.

## Validation

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/disk-cache
python Tests/RemoteArchive/build.py --cache-check
python Tests/RemoteArchive/test_disk_cache.py
python Tests/RemoteArchive/test_disk_cache_agent.py --exe .build/msvc/disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_agent.py --exe .build/msvc/disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_cli.py --exe .build/msvc/disk-cache/7z.exe
python Tests/RemoteArchive/test_redirect.py --exe .build/msvc/disk-cache/7z.exe --agent .build/msvc/disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_retry.py --exe .build/msvc/disk-cache/7z.exe --agent .build/msvc/disk-cache/agentcheck.exe
python Tests/RemoteArchive/test_cancel.py --exe .build/msvc/disk-cache/agentcheck.exe
python Tests/RemoteArchive/build.py
python Tests/RemoteArchive/test_remote.py --dll "$PWD/.build/msvc/disk-cache/7z.dll" --sevenzip "$PWD/.build/msvc/disk-cache/7z.exe"
python Tests/RemoteArchive/build_msvc.py --target filemanager --rebuild --bin-dir .build/msvc/disk-cache-local
python Tests/RemoteArchive/test_cache_layout.py --exe .build/msvc/disk-cache/7zFM.exe --local-exe .build/msvc/disk-cache-local/7zFM.exe
```

The cache transport suite passes 51 scenarios: fresh/verified opens, changed URLs,
manual linking, explicit bypass consent, known validator/size changes, stable
redirect scopes, rejected-link isolation, corrupt parts/manifests, generation-bound
parts, interrupted writes, unlimited/reduced limits, actual sharing-related I/O
failure, safe clear recovery, foreign files, junctions, multiple processes,
active-entry protection, abrupt termination, and cancellation during validation.

Ten Agent scenarios exercise actual ZIP and solid/non-solid 7z extraction/testing,
post-extraction estimates and independent no-ETag consent. A GNU client also reads
and extends an MSVC-produced cache, then the MSVC Agent reuses it. In the controlled
verified reopen cases, extraction used the saved parts with only the one-byte
validation probe downloaded. Fixture hashes match local inputs.

The existing 99 CLI, 60 Agent, 35 redirect, 71 retry, eight cancellation and
72 stream/prototype regressions pass. MSVC HTTP/local-only builds and portable
GNU builds succeed. Compiled Open URL/cache resources fit without control overlap;
the HTTP-only dialogs are absent from the local-only build.

Desktop visual acceptance remains pending because the Computer Use native
connection was unavailable and development was continued at the user's request.
Before publication, manually check Browse, saved settings across restart, partial
reuse confirmation, Clear with an archive open, dropdown readability and status
tooltips. Existing WinHTTP internal event retention after cancellation remains a
separate unresolved observation; see [the cancellation notes](17-responsive-cancellation.md).
