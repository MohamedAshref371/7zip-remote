# Open URL Defaults and Archive Type Selection

**Later update:** RAR and RAR5 are now represented by a single RAR choice with
internal generation detection. See [Unified RAR selection](11-rar-family.md).
The descriptions below record the preceding UI iteration.

The user requested ETag bypass enabled by default in the Open URL dialog and an
archive type selector for known formats.

- **Skip ETag verification** is now checked each time the dialog opens. It can
  be unchecked to restore strong ETag validation for that session.
- **Archive type** starts at **Auto**. The other choices are **ZIP**, **7z**,
  **RAR**, and **RAR5**. RAR generations use their corresponding engine handlers.
- Auto passes an empty type to the existing Agent for normal format detection.
  Explicit choices pass the corresponding engine type identifier.
- The selected type is stored in the remote folder link alongside the ETag
  policy. Refresh passes both choices to the newly opened session.
- The transport and console still verify ETags by default. This change applies
  to the Open URL dialog, without changing direct address-bar behavior.

## Validation

The HTTP-enabled File Manager and headless Agent check were rebuilt with MSVC.
All **34 Agent scenarios passed**, including explicit ZIP and 7z selection,
encrypted 7z, ZIP without ETag, and rejection of ZIP opened as 7z. The other cases
cover Auto, extraction/testing, read-only enforcement, source validation, and
cancellation. RAR/RAR5 handler registration is checked, but no RAR archive fixture
was available for an extraction test in this change.

Compiled dialog resources were checked for the archive type control and ETag
checkbox. Runtime dropdown behavior, initial checked state, and Refresh still
need manual desktop verification; these are not claimed as automated UI tests.

Retained results: [open-options-results.json](open-options-results.json).

## Development Build

Open `.build/msvc/open-options/7zFM.exe`, then choose **File → Open URL...**.
Keep `7z.dll` beside the executable. Rebuild with:

```powershell
python Tests/RemoteArchive/build_msvc.py --target filemanager --http --rebuild --agent-check --bin-dir .build/msvc/open-options
```

When building into a fresh directory, also place the engine and console there
for the Agent test harness (or build all targets with `--http --rebuild
--agent-check --bin-dir .build/msvc/open-options`). Then run:

```powershell
python Tests/RemoteArchive/test_agent.py --exe "$PWD/.build/msvc/open-options/agentcheck.exe"
```
