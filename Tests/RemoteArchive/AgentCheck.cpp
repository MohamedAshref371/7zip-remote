// Headless integration check using the exact Agent/File Manager objects.
#include "../../CPP/7zip/UI/FileManager/StdAfx.h"
#include <stdio.h>
#include "../../CPP/7zip/UI/Agent/Agent.h"
#include "../../CPP/Common/StringConvert.h"
#include "../../C/DllSecur.h"

extern HINSTANCE g_hInstance;

class CTestCallback Z7_final:
    public IFolderArchiveExtractCallback, public IArchiveOpenCallback,
    public ICryptoGetTextPassword, public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_3(IFolderArchiveExtractCallback, IArchiveOpenCallback, ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
public:
  bool Cancel = false;
  bool Failed = false;
};

Z7_COM7F_IMF(CTestCallback::SetTotal(UInt64)) { return Cancel ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetCompleted(const UInt64 *)) { return Cancel ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetTotal(const UInt64 *, const UInt64 *)) { return Cancel ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetCompleted(const UInt64 *, const UInt64 *)) { return Cancel ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::AskOverwrite(const wchar_t *, const FILETIME *, const UInt64 *,
    const wchar_t *, const FILETIME *, const UInt64 *, Int32 *answer))
{ *answer = NOverwriteAnswer::kYes; return S_OK; }
Z7_COM7F_IMF(CTestCallback::PrepareOperation(const wchar_t *, Int32, Int32, const UInt64 *)) { return S_OK; }
Z7_COM7F_IMF(CTestCallback::MessageError(const wchar_t *)) { Failed = true; return E_FAIL; }
Z7_COM7F_IMF(CTestCallback::SetOperationResult(Int32 result, Int32))
{ if (result != 0) Failed = true; return S_OK; }
Z7_COM7F_IMF(CTestCallback::CryptoGetTextPassword(BSTR *password))
{ *password = SysAllocString(L"fixture-password"); return *password ? S_OK : E_OUTOFMEMORY; }

#define CHECK_HR(call) do { const HRESULT hr_ = (call); if (hr_ != S_OK) { \
  fprintf(stderr, "line %u: HRESULT %08lx\n", __LINE__, (unsigned long)hr_); return 1; } } while (0)
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %u: check failed\n", __LINE__); return 1; } } while (0)

int __cdecl wmain(int argc, wchar_t **argv)
{
  if (argc < 3) return 2;
  g_hInstance = GetModuleHandleW(NULL);
  My_SetDefaultDllDirectories();
  const bool remote = IsHttpArchiveUrl(UString(argv[1]));
  CArchiveFolderManager *manager = new CArchiveFolderManager;
  CMyComPtr<IFolderManager> managerRef = manager;
  CMyComPtr<IFolderFolder> folder;
  {
    CTestCallback *callback = new CTestCallback;
    CMyComPtr<IProgress> progress = callback;
    if (argc > 3 && wcscmp(argv[3], L"cancel") == 0) callback->Cancel = true;
    const HRESULT result = managerRef->OpenFolderFile(NULL, argv[1], L"", &folder, progress);
    if (result != S_OK)
    {
      fprintf(stderr, "Open HRESULT %08lx\n", (unsigned long)result);
      fprintf(stderr, "%s\n", UnicodeStringToMultiByte(manager->GetRemoteError(), CP_UTF8).Ptr());
      return result == E_ABORT ? 3 : 1;
    }
    callback->Cancel = true;
  } // Destroy the open callback before subsequent reads: no retained raw pointer.
  CHECK_HR(folder->LoadItems());
  NWindows::NCOM::CPropVariant readOnly;
  CHECK_HR(folder->GetFolderProperty(kpidReadOnly, &readOnly));
  CHECK(readOnly.vt == VT_BOOL && (readOnly.boolVal != VARIANT_FALSE) == remote);

  CMyComPtr<IFolderFolder> docs;
  CHECK_HR(folder->BindToFolder(L"docs", &docs));
  CHECK(docs);
  CHECK_HR(docs->LoadItems());
  UInt32 count = 0;
  CHECK_HR(docs->GetNumberOfItems(&count));
  CHECK(count != 0);
  UInt32 selected = count;
  for (UInt32 i = 0; i < count; ++i)
  {
    NWindows::NCOM::CPropVariant name;
    CHECK_HR(docs->GetProperty(i, kpidName, &name));
    if (name.vt == VT_BSTR && wcscmp(name.bstrVal, L"target.txt") == 0) selected = i;
  }
  CHECK(selected < count);
  CMyComPtr<IFolderOperations> operations;
  CHECK_HR(docs.QueryInterface(IID_IFolderOperations, &operations));
  if (remote)
  {
    CHECK(operations->Delete(&selected, 1, NULL) == E_NOTIMPL);
    CHECK(operations->Rename(selected, L"renamed.txt", NULL) == E_NOTIMPL);
    CHECK(operations->CreateFolder(L"new-folder", NULL) == E_NOTIMPL);
    CHECK(operations->CopyFromFile(selected, L"does-not-exist", NULL) == E_NOTIMPL);
  }
  CMyComPtr<IFolderSetZoneIdMode> zone;
  CHECK_HR(docs.QueryInterface(IID_IFolderSetZoneIdMode, &zone));
  CHECK_HR(zone->SetZoneIdMode(NExtract::NZoneIdMode::kAll));
  CMyComPtr<IArchiveFolder> archive;
  CHECK_HR(docs.QueryInterface(IID_IArchiveFolder, &archive));
  CTestCallback *extract = new CTestCallback;
  CMyComPtr<IFolderArchiveExtractCallback> callback = extract;
  CHECK_HR(archive->Extract(&selected, 1, 0, 0, NExtract::NPathMode::kNoPaths,
      NExtract::NOverwriteMode::kOverwrite, argv[2], 0, callback));
  CHECK(!extract->Failed);
  CHECK_HR(archive->Extract(&selected, 1, 0, 0, NExtract::NPathMode::kNoPaths,
      NExtract::NOverwriteMode::kOverwrite, argv[2], 1, callback));
  CHECK(!extract->Failed);
  printf("Agent open/browse/extract/test/read-only checks passed.\n");
  return 0;
}
