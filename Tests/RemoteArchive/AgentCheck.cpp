// Headless integration check using the exact Agent/File Manager objects.
#include "../../CPP/7zip/UI/FileManager/StdAfx.h"
#include <stdio.h>
#include "../../CPP/7zip/UI/Agent/Agent.h"
#include "../../CPP/Common/StringConvert.h"
#include "../../C/DllSecur.h"
#include "../../CPP/7zip/UI/FileManager/OpenCallback.h"
#include "../../CPP/7zip/UI/FileManager/ExtractCallback.h"

extern HINSTANCE g_hInstance;

class CTestCallback Z7_final:
    public IFolderArchiveExtractCallback, public IArchiveOpenCallback,
    public ICryptoGetTextPassword, public CMyUnknownImp
    , public IRemoteProgress
{
  Z7_COM_UNKNOWN_IMP_4(IFolderArchiveExtractCallback, IArchiveOpenCallback, ICryptoGetTextPassword, IRemoteProgress)
  Z7_IFACE_COM7_IMP(IRemoteProgress)
  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
public:
  bool Cancel = false;
  bool Failed = false;
  bool NetworkDefined = false;
  bool NetworkMonotonic = true;
  UInt64 NetworkBytes = 0, NetworkSize = 0, NetworkRequests = 0;
  void PrintNetwork(const char *phase)
  {
    if (NetworkDefined)
      printf("NETWORK %s %llu %llu %llu\n", phase,
          (unsigned long long)NetworkBytes, (unsigned long long)NetworkSize,
          (unsigned long long)NetworkRequests);
  }
  void ResetNetwork()
  {
    NetworkDefined = false;
    NetworkBytes = NetworkSize = NetworkRequests = 0;
  }
};

Z7_COM7F_IMF(CTestCallback::SetNetworkProgress(UInt64 bytes, UInt64 size, UInt64 requests))
{
  if (NetworkDefined && (bytes < NetworkBytes || requests < NetworkRequests)) NetworkMonotonic = false;
  NetworkDefined = true;
  NetworkBytes = bytes;
  NetworkSize = size;
  NetworkRequests = requests;
  return S_OK;
}

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

static int CheckNetworkSync()
{
  COpenArchiveCallback open;
  COpenCallbackImp *bridge = new COpenCallbackImp;
  CMyComPtr<IArchiveOpenCallback> bridgeRef = bridge;
  bridge->Callback = &open;
  CMyComPtr<IRemoteProgress> progress;
  CHECK_HR(bridgeRef.QueryInterface(IID_IRemoteProgress, &progress));
  CHECK_HR(progress->SetNetworkProgress(123, 456, 2));
  CHECK(open.ProgressDialog.Sync._networkDefined);
  CHECK(open.ProgressDialog.Sync._networkBytes == 123);
  CHECK(open.ProgressDialog.Sync._networkSize == 456);
  CHECK(open.ProgressDialog.Sync._networkRequests == 2);
  CHECK(open.ProgressDialog.Sync._completedBytes == 0);
  bridge->Callback = NULL;

  CExtractCallbackImp *extract = new CExtractCallbackImp;
  CMyComPtr<IFolderArchiveExtractCallback> extractRef = extract;
  CProgressDialog dialog;
  extract->ProgressDialog = &dialog;
  CHECK_HR(extractRef.QueryInterface(IID_IRemoteProgress, &progress));
  CHECK_HR(progress->SetNetworkProgress(789, 1024, 3));
  CHECK(dialog.Sync._networkBytes == 789 && dialog.Sync._networkRequests == 3);
  CHECK(dialog.Sync._networkSize == 1024 && dialog.Sync._completedBytes == 0);
  return 0;
}

int __cdecl wmain(int argc, wchar_t **argv)
{
  if (argc < 3) return 2;
  g_hInstance = GetModuleHandleW(NULL);
  My_SetDefaultDllDirectories();
  CHECK(CheckNetworkSync() == 0);
  const bool remote = IsHttpArchiveUrl(UString(argv[1]));
  CArchiveFolderManager *manager = new CArchiveFolderManager;
  CMyComPtr<IFolderManager> managerRef = manager;
  const wchar_t *arcFormat = L"";
  bool rarFamilyCheck = false;
  for (int i = 3; i < argc; ++i)
  {
    if (wcscmp(argv[i], L"--skip-http-etag") == 0) manager->SetSkipHttpETag(true);
    if (wcsncmp(argv[i], L"--format=", 9) == 0) arcFormat = argv[i] + 9;
    if (wcscmp(argv[i], L"--rar-family-check") == 0) rarFamilyCheck = true;
  }
  CMyComPtr<IFolderFolder> folder;
  {
    CTestCallback *callback = new CTestCallback;
    CMyComPtr<IProgress> progress = callback;
    if (argc > 3 && wcscmp(argv[3], L"cancel") == 0) callback->Cancel = true;
    const HRESULT result = managerRef->OpenFolderFile(NULL, argv[1], arcFormat, &folder, progress);
    if (result != S_OK)
    {
      fprintf(stderr, "Open HRESULT %08lx\n", (unsigned long)result);
      fprintf(stderr, "%s\n", UnicodeStringToMultiByte(manager->GetRemoteError(), CP_UTF8).Ptr());
      return result == E_ABORT ? 3 : 1;
    }
    callback->Cancel = true;
    CHECK(callback->NetworkMonotonic);
    CHECK(callback->NetworkDefined == remote);
    callback->PrintNetwork("open");
  } // Destroy the open callback before subsequent reads: no retained raw pointer.
  CHECK_HR(folder->LoadItems());
  NWindows::NCOM::CPropVariant readOnly;
  CHECK_HR(folder->GetFolderProperty(kpidReadOnly, &readOnly));
  CHECK(readOnly.vt == VT_BOOL && (readOnly.boolVal != VARIANT_FALSE) == remote);

  if (rarFamilyCheck)
  {
    CMyComPtr<IGetFolderArcProps> getProps;
    CHECK_HR(folder.QueryInterface(IID_IGetFolderArcProps, &getProps));
    CMyComPtr<IFolderArcProps> props;
    CHECK_HR(getProps->GetFolderArcProps(&props));
    NWindows::NCOM::CPropVariant type;
    CHECK_HR(props->GetArcProp(0, kpidType, &type));
    CHECK(type.vt == VT_BSTR);
    const UString typeName(type.bstrVal);
    CHECK(typeName.IsEqualTo_Ascii_NoCase("rar") || typeName.IsEqualTo_Ascii_NoCase("rar5"));
    printf("Handler=%s\n", UnicodeStringToMultiByte(typeName, CP_UTF8).Ptr());
    UInt32 count = 0;
    CHECK_HR(folder->GetNumberOfItems(&count));
    CHECK(count != 0);
    CUIntVector indices;
    for (UInt32 i = 0; i < count; ++i) indices.Add(i);
    CMyComPtr<IArchiveFolder> archive;
    CHECK_HR(folder.QueryInterface(IID_IArchiveFolder, &archive));
    CTestCallback *extract = new CTestCallback;
    CMyComPtr<IFolderArchiveExtractCallback> callback = extract;
    CHECK_HR(archive->Extract(indices.ConstData(), indices.Size(), 0, 0, NExtract::NPathMode::kFullPaths,
        NExtract::NOverwriteMode::kOverwrite, argv[2], 0, callback));
    CHECK(!extract->Failed);
    CHECK_HR(archive->Extract(indices.ConstData(), indices.Size(), 0, 0, NExtract::NPathMode::kFullPaths,
        NExtract::NOverwriteMode::kOverwrite, argv[2], 1, callback));
    CHECK(!extract->Failed);
    printf("RAR-family browse/extract/test checks passed.\n");
    return 0;
  }

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
  CHECK(extract->NetworkMonotonic && extract->NetworkDefined == remote);
  extract->PrintNetwork("extract");
  extract->ResetNetwork();
  CHECK_HR(archive->Extract(&selected, 1, 0, 0, NExtract::NPathMode::kNoPaths,
      NExtract::NOverwriteMode::kOverwrite, argv[2], 1, callback));
  CHECK(!extract->Failed);
  CHECK(extract->NetworkMonotonic && extract->NetworkDefined == remote);
  extract->PrintNetwork("test");
  printf("Agent open/browse/extract/test/read-only checks passed.\n");
  return 0;
}
