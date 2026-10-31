# 0.6.0-dev — Development Preview

This work follows the published **v0.5.0** release. It is a development preview,
not a newly published release or a change to the v0.5.0 tag.

## Changes

- Clear operation stages: connecting, checking saved parts, reading archive
  headers, extracting and testing.
- A separate **Last discovered item** label while opening, distinct from the
  current extraction item. Unknown totals no longer display a synthetic 0%.
- Compact persistent exact-read cache: downloaded ranges are stored in one
  append-only data file with a rebuilt in-memory index. Existing v0.5.0 caches
  remain readable. No extra archive bytes are fetched to fill cache blocks.
- Part headers and payloads are integrity-checked. Incomplete tails are ignored;
  corrupt entries are downloaded again. Existing quota, ownership, lock and
  version-verification policies remain in effect. Quota eviction can discard an
  entire compact data file rather than individual small parts.
- About displays the remote-extension version separately from upstream 7-Zip.
- Refresh and URL renewal restore nested archive folders, including paths with
  trailing separators, while preserving the selected item.
- Repeatable byte/request/time benchmarks and portable packaging with licenses,
  checksums and source provenance.

## Running

Extract the full ZIP and launch **7zRemoteFM.exe**. Keep `7z.dll` beside it and
`7z.exe`. Choose **File → Open URL…**. This copy shares File Manager preferences
with other 7-Zip builds; no installation or shell registration is performed.

## Limits

Remote archives remain read-only and single-volume. URLs must point directly to
data on a server supporting HTTP byte ranges. Browser authentication and sharing
pages are unsupported. Exact reads reduce stream read-ahead, not archive-engine
buffering, metadata requirements or solid-compression dependencies. They may be
slower due to additional requests. Unverified cache reuse requires separate
consent and partial checks cannot prove entire-file equality.

## Credits

The original 7-Zip application and compression/archive engine are by **Igor
Pavlov and upstream contributors**. The remote extension, testing and tooling
were developed with substantial contributions from **OpenAI Codex**, under the
repository maintainer's direction. Upstream licenses and notices apply.
