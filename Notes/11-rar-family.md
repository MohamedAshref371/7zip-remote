# Unified RAR Selection

The Open URL dialog now offers **Auto**, **ZIP**, **7z**, and a single **RAR**
choice. Users do not need to identify the RAR generation.

For a remote archive selected as RAR, the Agent uses the existing engine's format
detection with all handlers except RAR and RAR5 excluded. Normal signature
detection selects the generation; ZIP and unrelated formats are not accepted
under that choice. Auto remains unrestricted. Refresh retains the generic RAR
selection through the existing remote folder link. The dialog's default ETag
bypass setting is unchanged.

## Verification

- **35 Agent scenarios passed**, including a new ZIP-as-RAR rejection case.
- **8 RAR-family scenarios passed** using existing local SharpCompress test
  fixtures supplied to the test command. Both RAR generations were tested with
  Auto, the shared RAR selection, an unrelated URL extension, and ETag bypass.
- The harness verified the actual handler name (`rar` or `rar5`), browsed the
  archive, extracted and tested its contents, and compared all output file hashes
  against normal local 7-Zip extraction.
- Fixtures are not downloaded or added to this repository by the new test.
  Fixture hashes and test results are retained in [rar-family-results.json](rar-family-results.json).

This covers single-volume samples. It does not establish support for every RAR
feature, encrypted RAR, multi-volume archives, or the complete desktop flow.
The dropdown interaction itself still requires manual verification.

## Build and Test

The new GUI executable is `.build/msvc/rar-family/7zFM.exe`, beside its engine DLL.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --rebuild --agent-check --bin-dir .build/msvc/rar-family
python Tests/RemoteArchive/test_agent.py --exe "$PWD/.build/msvc/rar-family/agentcheck.exe"
python Tests/RemoteArchive/test_rar_family.py --rar4 'path/to/legacy.rar' --rar5 'path/to/modern.rar'
```

The archive paths in the final command are placeholders for local fixtures.
The unified selection applies to remote File Manager/Agent opening; this change
does not alter the standard console's existing format-switch semantics.
