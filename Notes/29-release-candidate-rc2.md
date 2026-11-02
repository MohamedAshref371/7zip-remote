# v1.0.0-rc2 — Windows x64 Release Candidate

This candidate is based on upstream 7-Zip 26.05. It adds language support,
Arabic layout for Remote Archive dialogs, and configurable HTTP download blocks.
It remains a prerelease while clean-machine and broader real-server acceptance
for stable v1.0.0 are pending.

## Changes since v1.0.0-rc1

- Ship the upstream language files with the portable package. New Remote Archive
  text is available in Arabic and English; other languages show English for those
  additions until translated.
- Mirror the Open URL, cache settings, and URL renewal dialogs when Arabic is
  selected. The rest of the File Manager retains its existing layout.
- Offer 32, 64, 128, and 256 KiB HTTP download blocks in Open URL. The default
  remains 256 KiB. The choice is retained for Refresh and URL renewal, and disk
  caches for different block sizes are kept separate. Exact-read mode remains
  independent of the block-size choice.
- Add integration and layout checks for these changes to the release gate.

## Supported scope

Windows x64; direct HTTP(S) links to single-volume archives on byte-range
servers; browsing and selected extraction/testing in read-only remote sessions.
ZIP, 7z, RAR/RAR5, TAR and ISO have controlled integration coverage. Strong ETag
verification is the default; bypass requires a deliberate choice. Browser
login/cookies, sharing-page resolution, remote writes, and multi-volume remote
archives are unsupported. Solid archives can need most of their data.

## Validation and remaining acceptance

The Windows release gate builds the HTTP and local-only binaries, runs the
Remote Archive suites, packages binary and source ZIPs from a clean commit,
and verifies their hashes and source provenance. Release results should be
recorded against the exact rc2 commit before publication. The local Windows x64
gate passed on 2026-11-02 with the rc2 version in the binaries. A separate
10-scenario compatibility run passed against the older, pre-compact cache format.

For rc2 publication, smoke-test the portable File Manager with Arabic selected:
open Open URL, cache settings, and URL renewal; verify their reading direction.
Open a RAR archive with each block-size choice and check that 256 KiB is the
initial default. Record the outcome beside the release gate result.

Before stable v1.0.0, test the portable ZIP on a clean Windows x64 account
without build tools, and check several real servers used by the intended
audience, including signed URLs, redirects, ETag variations and relevant proxies.
The [rc1 notes](27-release-candidate.md) record earlier controlled and
real-server checks; those results do not replace rc2 acceptance.
