#ifndef ZIP7_INC_REMOTE_CACHE_DIALOG_H
#define ZIP7_INC_REMOTE_CACHE_DIALOG_H
#include "../../../Windows/HttpDiskCache.h"
#include "../../../Windows/Shell.h"
#include "../Common/RemoteSelection.h"
#include "ComboDialogRes.h"

inline void LoadRemoteCacheSettings(NWindows::CHttpDiskCacheOptions &options)
{
  wchar_t local[32768];
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
  if (length && length < 32768) options.Directory = std::wstring(local) + L"\\7-Zip\\RemoteCache";
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip\\FM\\RemoteCache", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
  DWORD enabled = 0, type = 0, size = sizeof(enabled);
  if (RegQueryValueExW(key, L"Enabled", NULL, &type, (BYTE *)&enabled, &size) == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(enabled))
    options.Enabled = enabled != 0;
  ULONGLONG limit = options.MaxBytes;
  size = sizeof(limit);
  if (RegQueryValueExW(key, L"MaxBytes", NULL, &type, (BYTE *)&limit, &size) == ERROR_SUCCESS && type == REG_QWORD && size == sizeof(limit)) options.MaxBytes = limit;
  size = sizeof(local);
  if (RegQueryValueExW(key, L"Directory", NULL, &type, (BYTE *)local, &size) == ERROR_SUCCESS
      && type == REG_SZ && size >= sizeof(wchar_t) && size <= sizeof(local) && !(size % sizeof(wchar_t))
      && local[size / sizeof(wchar_t) - 1] == 0) options.Directory = local;
  RegCloseKey(key);
}

inline bool SaveRemoteCacheSettings(const NWindows::CHttpDiskCacheOptions &options)
{
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip\\FM\\RemoteCache", 0, NULL, 0,
      KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return false;
  const DWORD enabled = options.Enabled ? 1 : 0;
  const bool ok = RegSetValueExW(key, L"Enabled", 0, REG_DWORD, (const BYTE *)&enabled, sizeof(enabled)) == ERROR_SUCCESS
      && RegSetValueExW(key, L"MaxBytes", 0, REG_QWORD, (const BYTE *)&options.MaxBytes, sizeof(options.MaxBytes)) == ERROR_SUCCESS
      && RegSetValueExW(key, L"Directory", 0, REG_SZ, (const BYTE *)options.Directory.c_str(),
          (DWORD)((options.Directory.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  RegCloseKey(key);
  return ok;
}

class CRemoteCacheDialog Z7_final: public NWindows::NControl::CModalDialog
{
  void Usage()
  {
    try
    {
    UString path;
    GetItemText(IDC_CACHE_PATH, path);
    ULONGLONG used = 0;
    const auto entries = NWindows::CHttpDiskCache::Catalog(path.Ptr());
    for (const auto &entry : entries) used += entry.Used;
    UString text = L"Used: ";
    text += RemoteSelectionSize(used);
    text += L" in "; text.Add_UInt64(entries.size()); text += L" saved archives";
    SetItemText(IDC_CACHE_USAGE, text);
    }
    catch (...) { SetItemText(IDC_CACHE_USAGE, L"Cache usage unavailable"); }
  }
  bool OnInit() Z7_override
  {
    SetItemText(IDC_CACHE_PATH, Options.Directory.c_str());
    CheckButton(IDC_CACHE_ENABLED, Options.Enabled);
    CheckButton(IDC_CACHE_LIMIT, Options.MaxBytes != 0);
    UString limit; limit.Add_UInt64(Options.MaxBytes ? Options.MaxBytes / (1 << 20) : 1024);
    SetItemText(IDC_CACHE_MIB, limit);
    Usage(); NormalizeSize();
    return CModalDialog::OnInit();
  }
  bool OnCommand(unsigned code, unsigned id, LPARAM param) Z7_override
  {
    if (id == IDC_CACHE_PATH && code == EN_CHANGE) { Usage(); return true; }
    return CModalDialog::OnCommand(code, id, param);
  }
  bool OnButtonClicked(unsigned id, HWND hwnd) Z7_override
  {
    if (id == IDC_CACHE_BROWSE)
    {
      UString current, chosen;
      GetItemText(IDC_CACHE_PATH, current);
      if (NWindows::NShell::BrowseForFolder(*this, L"Choose the cache folder", current, chosen))
      { SetItemText(IDC_CACHE_PATH, chosen); Usage(); }
      return true;
    }
    if (id == IDC_CACHE_CLEAR)
    {
      if (MessageBoxW(*this, L"Clear saved archive data in this cache folder? Active archives and unrelated files will be kept.",
          L"Clear remote cache", MB_YESNO | MB_ICONQUESTION) != IDYES) return true;
      UString path; GetItemText(IDC_CACHE_PATH, path);
      unsigned skipped = 0;
      bool ok = false;
      try { ok = NWindows::CHttpDiskCache::Clear(path.Ptr(), skipped); }
      catch (...) { /* Optional cache management must not close the application. */ }
      UString text = ok ? L"Cache cleared. Active archives retained: " : L"Cache could not be fully cleared. Check the folder or try again when it is idle. Active archives retained: ";
      text.Add_UInt32(skipped);
      MessageBoxW(*this, text, L"Remote archive cache", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
      Usage(); return true;
    }
    return CModalDialog::OnButtonClicked(id, hwnd);
  }
  void OnOK() Z7_override
  {
    UString path; GetItemText(IDC_CACHE_PATH, path);
    UINT limit = 0;
    if (IsButtonCheckedBool(IDC_CACHE_LIMIT) && (!GetItemUInt(IDC_CACHE_MIB, limit) || !limit))
    { MessageBoxW(*this, L"Enter a positive cache limit in MiB, or uncheck the limit.", L"Remote cache", MB_OK | MB_ICONWARNING); return; }
    Options.Enabled = IsButtonCheckedBool(IDC_CACHE_ENABLED);
    Options.Directory = path.Ptr();
    Options.MaxBytes = IsButtonCheckedBool(IDC_CACHE_LIMIT) ? (ULONGLONG)limit << 20 : 0;
    bool writable = !Options.Enabled;
    try { if (Options.Enabled) writable = NWindows::CHttpDiskCache::Prepare(Options.Directory); }
    catch (...) { writable = false; }
    if (!writable)
    { MessageBoxW(*this, L"Choose a writable folder without junctions or symbolic links. An existing unrelated cache folder will not be overwritten.", L"Remote cache", MB_OK | MB_ICONWARNING); return; }
    if (!SaveRemoteCacheSettings(Options))
    { MessageBoxW(*this, L"Cannot save cache settings.", L"Remote cache", MB_OK | MB_ICONWARNING); return; }
    CModalDialog::OnOK();
  }
public:
  NWindows::CHttpDiskCacheOptions Options;
  INT_PTR Create(HWND parent) { return CModalDialog::Create(IDD_REMOTE_CACHE, parent); }
};
#endif
