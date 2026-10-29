# ISO Selection in Open URL

The archive type dropdown now offers Auto, ZIP, 7z, RAR, and ISO.
Selecting ISO passes `iso` to the existing archive handler selection. Refresh
retains this selection through the existing remote folder link. Auto and the
default ETag bypass setting are unchanged.

The Windows x64 engine, console, File Manager, and Agent check built successfully
with MSVC. The delivered engine lists its ISO handler. No ISO image extraction
or desktop dropdown interaction was tested for this change.

Updated executable: `.build/msvc/iso-options/7zFM.exe`, beside `7z.dll`.

```powershell
python Tests/RemoteArchive/build_msvc.py --http --agent-check --bin-dir .build/msvc/iso-options
```
