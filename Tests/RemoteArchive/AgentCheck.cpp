// Headless integration check using the exact Agent/File Manager objects.
#include "../../CPP/7zip/UI/FileManager/StdAfx.h"
#include <stdio.h>
#include <thread>
#include "../../CPP/7zip/UI/Agent/Agent.h"
#include "../../CPP/Common/StringConvert.h"
#include "../../C/DllSecur.h"
#include "../../CPP/7zip/UI/FileManager/OpenCallback.h"
#include "../../CPP/7zip/UI/FileManager/ExtractCallback.h"

extern HINSTANCE g_hInstance;
static const wchar_t *g_TestPassword = L"fixture-password";

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
  DWORD CancelStarted = 0;
  unsigned CancelDelay = 0;
  void CancelAfter(unsigned delay) { CancelStarted = GetTickCount(); CancelDelay = delay; }
  bool IsCancelled() const { return Cancel || (CancelDelay && GetTickCount() - CancelStarted >= CancelDelay); }
  bool NetworkDefined = false;
  bool NetworkMonotonic = true;
  UInt64 NetworkBytes = 0, NetworkSize = 0, NetworkRequests = 0;
  bool EstimateDefined = false;
  bool EstimateCaptured = false;
  UInt64 InitialEstimate = 0;
  void PrintNetwork(const char *phase)
  {
    if (NetworkDefined)
      printf("NETWORK %s %llu %llu %llu\n", phase,
          (unsigned long long)NetworkBytes, (unsigned long long)NetworkSize,
          (unsigned long long)NetworkRequests);
    if (NetworkDefined)
      printf("ESTIMATE %s %u %llu\n", phase, EstimateDefined ? 1u : 0u,
          (unsigned long long)InitialEstimate);
  }
  void ResetNetwork()
  {
    NetworkDefined = false;
    NetworkBytes = NetworkSize = NetworkRequests = 0;
    EstimateDefined = EstimateCaptured = false;
    InitialEstimate = 0;
  }
};

Z7_COM7F_IMF(CTestCallback::SetNetworkEstimate(UInt64 bytes, Int32 defined))
{
  EstimateDefined = defined != 0;
  if (EstimateDefined && !EstimateCaptured)
  {
    InitialEstimate = bytes;
    EstimateCaptured = true;
  }
  return S_OK;
}

Z7_COM7F_IMF(CTestCallback::SetNetworkProgress(UInt64 bytes, UInt64 size, UInt64 requests))
{
  if (NetworkDefined && (bytes < NetworkBytes || requests < NetworkRequests)) NetworkMonotonic = false;
  NetworkDefined = true;
  NetworkBytes = bytes;
  NetworkSize = size;
  NetworkRequests = requests;
  return S_OK;
}

Z7_COM7F_IMF(CTestCallback::SetTotal(UInt64)) { return IsCancelled() ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetCompleted(const UInt64 *)) { return IsCancelled() ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetTotal(const UInt64 *, const UInt64 *)) { return IsCancelled() ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::SetCompleted(const UInt64 *, const UInt64 *)) { return IsCancelled() ? E_ABORT : S_OK; }
Z7_COM7F_IMF(CTestCallback::AskOverwrite(const wchar_t *, const FILETIME *, const UInt64 *,
    const wchar_t *, const FILETIME *, const UInt64 *, Int32 *answer))
{ *answer = NOverwriteAnswer::kYes; return S_OK; }
Z7_COM7F_IMF(CTestCallback::PrepareOperation(const wchar_t *, Int32, Int32, const UInt64 *)) { return S_OK; }
Z7_COM7F_IMF(CTestCallback::MessageError(const wchar_t *)) { Failed = true; return E_FAIL; }
Z7_COM7F_IMF(CTestCallback::SetOperationResult(Int32 result, Int32))
{ if (result != 0) Failed = true; return S_OK; }
Z7_COM7F_IMF(CTestCallback::CryptoGetTextPassword(BSTR *password))
{ *password = SysAllocString(g_TestPassword); return *password ? S_OK : E_OUTOFMEMORY; }

#define CHECK_HR(call) do { const HRESULT hr_ = (call); if (hr_ != S_OK) { \
  fprintf(stderr, "line %u: HRESULT %08lx\n", __LINE__, (unsigned long)hr_); return 1; } } while (0)
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %u: check failed\n", __LINE__); return 1; } } while (0)

struct CCancelClock
{
  DWORD Started, Owner;
  bool WrongThread;
  CCancelClock(): Started(GetTickCount()), Owner(GetCurrentThreadId()), WrongThread(false) {}
  static bool Check(void *context)
  {
    CCancelClock &self = *static_cast<CCancelClock *>(context);
    if (GetCurrentThreadId() != self.Owner) self.WrongThread = true;
    return GetTickCount() - self.Started >= 200;
  }
};

static int CheckTransportCancel(const wchar_t *url, bool eventMode, unsigned repeats)
{
  DWORD baseline = 0, after = 0;
  CHECK(GetProcessHandleCount(GetCurrentProcess(), &baseline));
  for (unsigned i = 0; i <= repeats; ++i)
  {
    CCancelClock clock;
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event);
    std::thread timer;
    if (eventMode) timer = std::thread([event]() { Sleep(200); SetEvent(event); });
    HRESULT result;
    bool cancelledMessage = false, emptyRange = false;
    {
      NWindows::CHttpRangeSource source;
      NWindows::CHttpRangeOptions options;
      options.TimeoutMs = 10000;
      options.RequestDeadlineMs = 10000;
      if (eventMode) options.CancelEvent = event;
      else { options.CancelCheck = CCancelClock::Check; options.CancelContext = &clock; }
      result = source.Open(url, options);
      std::vector<unsigned char> data;
      if (result == S_OK) result = source.Fetch(0, (unsigned)(source.Size < 262144 ? source.Size : 262144), data);
      cancelledMessage = source.Error == "Cancelled.";
      emptyRange = data.empty();
    }
    if (timer.joinable()) timer.join();
    CloseHandle(event);
    CHECK(result == E_ABORT && cancelledMessage && emptyRange);
    CHECK(!clock.WrongThread);
    CHECK(GetTickCount() - clock.Started < 1500);
  }
  // Diagnostic only: process-wide counts include WinHTTP-retained internal
  // events, also observed in a minimal WinHTTP-only client. The Python fixture
  // asserts that every blocked socket is closed; each request drains its final
  // HANDLE_CLOSING callback before releasing the context and read buffers.
  CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
  printf("CANCEL_CHECK %u %lu %lu\n", repeats + 1, (unsigned long)baseline, (unsigned long)after);
  return 0;
}

static int CheckNetworkSync()
{
  CHttpInStream stream;
  stream.Source.Size = ((UInt64)1 << 40) + 123;
  UInt64 estimate;
  std::vector<CHttpReadRange> ranges = {{1, 2}, {1, 2}, {262140, 8}};
  CHECK(stream.EstimateMissingBytes(ranges, estimate) && estimate == 524288);
  ranges = {{stream.Source.Size - 1, 1}};
  CHECK(stream.EstimateMissingBytes(ranges, estimate) && estimate == 123);
  ranges = {{stream.Source.Size, 1}};
  CHECK(!stream.EstimateMissingBytes(ranges, estimate));
  ranges = {{(UInt64)0 - 1, 2}};
  CHECK(!stream.EstimateMissingBytes(ranges, estimate));
  ranges.clear();
  CHECK(stream.EstimateMissingBytes(ranges, estimate) && estimate == 0);
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
  CHECK_HR(progress->SetNetworkEstimate(200, 1));
  CHECK(open.ProgressDialog.Sync._networkEstimateDefined && open.ProgressDialog.Sync._networkEstimate == 200);
  bridge->Callback = NULL;

  CExtractCallbackImp *extract = new CExtractCallbackImp;
  CMyComPtr<IFolderArchiveExtractCallback> extractRef = extract;
  CProgressDialog dialog;
  extract->ProgressDialog = &dialog;
  CHECK_HR(extractRef.QueryInterface(IID_IRemoteProgress, &progress));
  CHECK_HR(progress->SetNetworkProgress(789, 1024, 3));
  CHECK(dialog.Sync._networkBytes == 789 && dialog.Sync._networkRequests == 3);
  CHECK(dialog.Sync._networkSize == 1024 && dialog.Sync._completedBytes == 0);
  CHECK_HR(progress->SetNetworkEstimate(1000, 1));
  CHECK(dialog.Sync._networkEstimateDefined && dialog.Sync._networkEstimate == 1000);
  CHECK_HR(progress->SetNetworkEstimate(0, 0));
  CHECK(!dialog.Sync._networkEstimateDefined);
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
  bool estimateArchiveCheck = false;
  const wchar_t *skipItem = NULL;
  unsigned cancelOpenMs = 0, cancelExtractMs = 0, transportRepeats = 0;
  bool cancelEventMode = false;
  for (int i = 3; i < argc; ++i)
  {
    if (wcscmp(argv[i], L"--transport-deadline-check") == 0)
    {
      NWindows::CHttpRangeSource source;
      NWindows::CHttpRangeOptions options;
      options.TimeoutMs = 10000;
      options.RequestDeadlineMs = 250;
      const DWORD started = GetTickCount();
      CHECK(source.Open(argv[1], options) == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
      CHECK(GetTickCount() - started < 1500);
      CHECK(source.Requests >= 2 && source.Requests <= 4);
      printf("REDIRECT_DEADLINE_OK\n");
      return 0;
    }
    if (wcscmp(argv[i], L"--skip-http-etag") == 0) manager->SetSkipHttpETag(true);
    if (wcsncmp(argv[i], L"--format=", 9) == 0) arcFormat = argv[i] + 9;
    if (wcscmp(argv[i], L"--rar-family-check") == 0) rarFamilyCheck = true;
    if (wcscmp(argv[i], L"--estimate-archive-check") == 0) estimateArchiveCheck = true;
    if (wcsncmp(argv[i], L"--password=", 11) == 0) g_TestPassword = argv[i] + 11;
    if (wcsncmp(argv[i], L"--skip-item=", 12) == 0) skipItem = argv[i] + 12;
    if (wcsncmp(argv[i], L"--cancel-open-ms=", 17) == 0) cancelOpenMs = (unsigned)_wtoi(argv[i] + 17);
    if (wcsncmp(argv[i], L"--cancel-extract-ms=", 20) == 0) cancelExtractMs = (unsigned)_wtoi(argv[i] + 20);
    if (wcsncmp(argv[i], L"--transport-repeats=", 20) == 0) transportRepeats = (unsigned)_wtoi(argv[i] + 20);
    if (wcscmp(argv[i], L"--cancel-event") == 0) cancelEventMode = true;
  }
  if (transportRepeats) return CheckTransportCancel(argv[1], cancelEventMode, transportRepeats);
  CMyComPtr<IFolderFolder> folder;
  {
    CTestCallback *callback = new CTestCallback;
    CMyComPtr<IProgress> progress = callback;
    if (argc > 3 && wcscmp(argv[3], L"cancel") == 0) callback->Cancel = true;
    if (cancelOpenMs) callback->CancelAfter(cancelOpenMs);
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
  CHECK(readOnly.vt == VT_BOOL);
  if (remote) CHECK(readOnly.boolVal != VARIANT_FALSE);

  if (rarFamilyCheck || estimateArchiveCheck)
  {
    CMyComPtr<IGetFolderArcProps> getProps;
    CHECK_HR(folder.QueryInterface(IID_IGetFolderArcProps, &getProps));
    CMyComPtr<IFolderArcProps> props;
    CHECK_HR(getProps->GetFolderArcProps(&props));
    NWindows::NCOM::CPropVariant type;
    CHECK_HR(props->GetArcProp(0, kpidType, &type));
    CHECK(type.vt == VT_BSTR);
    const UString typeName(type.bstrVal);
    CHECK(typeName.IsEqualTo_Ascii_NoCase("rar") || typeName.IsEqualTo_Ascii_NoCase("rar5")
        || (estimateArchiveCheck && typeName.IsEqualTo_Ascii_NoCase("iso")));
    printf("Handler=%s\n", UnicodeStringToMultiByte(typeName, CP_UTF8).Ptr());
    UInt32 count = 0;
    CHECK_HR(folder->GetNumberOfItems(&count));
    CHECK(count != 0);
    CUIntVector indices;
    for (UInt32 i = 0; i < count; ++i)
    {
      NWindows::NCOM::CPropVariant name;
      CHECK_HR(folder->GetProperty(i, kpidName, &name));
      if (skipItem && name.vt == VT_BSTR && wcscmp(name.bstrVal, skipItem) == 0) continue;
      indices.Add(i);
    }
    if (estimateArchiveCheck)
    {
      // Check the actual metadata plan for the last data-bearing engine item,
      // independently of HTTP cache hits for small compressed fixtures.
      CAgentFolder *spec = static_cast<CAgentFolder *>((IFolderFolder *)folder);
      IInArchive *engine = spec->_agentSpec->GetArchive();
      CMyComPtr<IArchiveReadRanges> plan;
      CHECK_HR(engine->QueryInterface(IID_IArchiveReadRanges, (void **)&plan));
      UInt32 numItems = 0;
      CHECK_HR(engine->GetNumberOfItems(&numItems));
      for (UInt32 i = numItems; i > 0; --i)
      {
        NWindows::NCOM::CPropVariant dir, size;
        CHECK_HR(engine->GetProperty(i - 1, kpidIsDir, &dir));
        CHECK_HR(engine->GetProperty(i - 1, kpidSize, &size));
        if (dir.vt != VT_BOOL || dir.boolVal || size.vt != VT_UI8 || !size.uhVal.QuadPart) continue;
        CRemoteReadRangeSink *sink = new CRemoteReadRangeSink;
        CMyComPtr<IArchiveReadRangeSink> sinkRef = sink;
        const HRESULT result = plan->GetItemReadRanges(i - 1, sinkRef);
        printf("READ_PLAN %u %u\n", result == S_OK ? 1u : 0u, (unsigned)sink->Ranges.size());
        for (size_t j = 0; j < sink->Ranges.size(); ++j)
          printf("READ_RANGE %llu %llu\n", (unsigned long long)sink->Ranges[j].Start,
              (unsigned long long)sink->Ranges[j].Size);
        break;
      }
    }
    CMyComPtr<IArchiveFolder> archive;
    CHECK_HR(folder.QueryInterface(IID_IArchiveFolder, &archive));
    CTestCallback *extract = new CTestCallback;
    CMyComPtr<IFolderArchiveExtractCallback> callback = extract;
    CHECK_HR(archive->Extract(indices.ConstData(), indices.Size(), 0, 0, NExtract::NPathMode::kFullPaths,
        NExtract::NOverwriteMode::kOverwrite, argv[2], 0, callback));
    CHECK(!extract->Failed);
    extract->PrintNetwork("extract");
    extract->ResetNetwork();
    CHECK_HR(archive->Extract(indices.ConstData(), indices.Size(), 0, 0, NExtract::NPathMode::kFullPaths,
        NExtract::NOverwriteMode::kOverwrite, argv[2], 1, callback));
    CHECK(!extract->Failed);
    extract->PrintNetwork("test");
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
  if (cancelExtractMs)
  {
    printf("EXTRACT_READY\n");
    fflush(stdout);
    CHECK(getchar() == 'g');
    extract->CancelAfter(cancelExtractMs);
  }
  const HRESULT extraction = archive->Extract(&selected, 1, 0, 0, NExtract::NPathMode::kNoPaths,
      NExtract::NOverwriteMode::kOverwrite, argv[2], 0, callback);
  if (cancelExtractMs)
  {
    CHECK(extraction == E_ABORT);
    printf("Cancelled.\n");
    return 3;
  }
  CHECK_HR(extraction);
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
