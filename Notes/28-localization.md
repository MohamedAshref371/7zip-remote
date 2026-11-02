# Shipped translations and remote UI localization

The root `Lang/` directory was extracted without running the official Windows
installer, `7z2605-x64.exe`, from Igor Pavlov's 7-Zip 26.05 GitHub release.
The installer contains **92 `.txt` language files plus `en.ttt`**, the English
translator reference. This is the actual payload count; the website's advertised
87 languages is not used to discard files. Regional variants count as files.
Original translator credits and all original entries are preserved.

`Tests/RemoteArchive/lang_upstream.json` records the official download URL,
installer SHA-256 and the unmodified SHA-256 of every extracted file. Only
`ar.txt` and `en.ttt` have additional remote strings appended. All other files
remain byte-for-byte identical to the installer payload.

## Behavior

- The MSVC File Manager build copies `Lang/` beside the executable. The portable
  packager includes every language file and records each file in its manifest
  and checksums. The release verifier checks the exact language payload.
- Choose **Tools > Options > Language**. The existing 7-Zip language selection
  and registry behavior are retained. Keep `Lang/` beside `7zRemoteFM.exe`.
- When Arabic is selected, the remote Open URL, cache settings, and URL renewal
  dialogs use mirrored RTL templates. The File Manager and other 7-Zip dialogs
  retain their existing layout; URL input retains LTR reading order. Switching
  language affects newly opened remote dialogs without restarting the File Manager.
- The remote extension has 134 Arabic strings and matching English reference
  strings: menus, Open URL, cache dialogs and status, URL renewal, selection
  estimates, opening/extraction progress, About, and HTTP diagnostics on opening.
- Other languages retain their upstream translations; remote additions fall back
  to English until corresponding remote entries are supplied. Selecting English
  or running without `Lang/` also uses the built-in English fallback.
- Transport diagnostics and command-line output remain canonical English.
  Translation happens at GUI display boundaries, without altering URLs, format
  names, cache keys, ETags, byte counts or HTTP status codes. Unknown messages
  are preserved exactly. This does not add localization to the CLI.

## Adding a translation

`CPP/7zip/UI/FileManager/RemoteLangStrings.inc` assigns stable IDs starting at
900000 to exact English strings. Never renumber existing entries; append new
entries. Append the same ID and a UTF-8 translation to the target `Lang/*.txt`.
Retain CRLF line endings, ascending IDs, `\\n` escapes, and all `{0}` / `{1}`
placeholders. `Lang/en.ttt` is the English reference. These high IDs are language
keys, not Win32 resource/control IDs. `RemoteLang.h` supplies built-in English
when a translation is absent and formats placeholders without reinterpreting
inserted values. Dialog captions are localized before user values are populated.

## Validation

`python Tests/RemoteArchive/test_localization.py` validates every language file,
original payload hashes, IDs and Arabic/English placeholder parity. On Windows,
`--agent PATH/agentcheck.exe` additionally runs the real upstream `CLang` parser
and remote lookup/formatting tests for all files, including Arabic, missing-key
fallbacks and parameterized HTTP status diagnostics. Both checks are included
in the release gate. The release verifier requires the full language payload.

Arabic visual acceptance on Windows should check the mirrored remote dialogs,
long warnings, mixed Arabic / URL text, and progress labels at common DPI
settings. This change does not introduce a File Manager-wide RTL layout engine.
