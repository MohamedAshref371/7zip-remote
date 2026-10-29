// Experimental harness for the existing IInArchive engine; not the main 7z CLI.
#include "../../../Common/Common.h"
#include "../../../Common/MyInitGuid.h"
#include "../../Archive/IArchive.h"
#include "../../IPassword.h"
#include "../../Common/FileStreams.h"
#include "../../Common/HttpInStream.h"
#include "../../Common/TracingInStream.h"

#include <cstdio>
#include <string>
#include <vector>
#include <limits>
#include <thread>
#include <stdexcept>

namespace {
HANDLE g_Cancel = NULL;
BOOL WINAPI ConsoleHandler(DWORD code)
{
  if (code != CTRL_C_EVENT && code != CTRL_BREAK_EVENT) return FALSE;
  if (g_Cancel) SetEvent(g_Cancel);
  return TRUE;
}

struct CCancelScope
{
  HANDLE Event = CreateEventW(NULL, TRUE, FALSE, NULL);
  HANDLE Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
  std::thread Timer;
  CCancelScope()
  {
    if (!Event || !Stop)
    {
      if (Event) CloseHandle(Event);
      if (Stop) CloseHandle(Stop);
      throw std::runtime_error("Cannot create cancellation events.");
    }
    g_Cancel = Event;
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
  }
  void After(unsigned delay)
  {
    Timer = std::thread([this, delay]() {
      if (WaitForSingleObject(Stop, delay) == WAIT_TIMEOUT) SetEvent(Event);
    });
  }
  ~CCancelScope()
  {
    SetEvent(Stop);
    if (Timer.joinable()) Timer.join();
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    g_Cancel = NULL;
    CloseHandle(Event);
    CloseHandle(Stop);
  }
};

std::string Utf8(const std::wstring &value)
{
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
      NULL, 0, NULL, NULL);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
      &result[0], size, NULL, NULL);
  return result;
}

std::string Json(const std::string &value)
{
  std::string result = "\"";
  for (unsigned char c : value)
  {
    if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 32)
    {
      char escape[7];
      snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
      result += escape;
    }
    else result += static_cast<char>(c);
  }
  return result + '"';
}

struct CProperty
{
  PROPVARIANT Value;
  CProperty() { PropVariantInit(&Value); }
  ~CProperty() { PropVariantClear(&Value); }
};

class COpenCallback Z7_final:
  public IArchiveOpenCallback, public ICryptoGetTextPassword, public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_2(IArchiveOpenCallback, ICryptoGetTextPassword)
public:
  std::wstring Password;
  bool HasPassword = false;
};

HRESULT CheckCancel()
{
  return WaitForSingleObject(g_Cancel, 0) == WAIT_OBJECT_0 ? E_ABORT : S_OK;
}

Z7_COM7F_IMF(COpenCallback::SetTotal(const UInt64 *, const UInt64 *)) { return CheckCancel(); }
Z7_COM7F_IMF(COpenCallback::SetCompleted(const UInt64 *, const UInt64 *)) { return CheckCancel(); }
Z7_COM7F_IMF(COpenCallback::CryptoGetTextPassword(BSTR *password))
{
  *password = NULL;
  return HasPassword ? StringToBstr(Password.c_str(), password) : E_ACCESSDENIED;
}

class CExtractCallback Z7_final:
  public IArchiveExtractCallback, public ICryptoGetTextPassword, public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_3(IArchiveExtractCallback, IProgress, ICryptoGetTextPassword)
public:
  CMyComPtr<ISequentialOutStream> Output;
  UInt32 Index = 0;
  bool Completed = false;
  bool Failed = false;
  bool CurrentSelected = false;
  bool HasPassword = false;
  std::wstring Password;
};

Z7_COM7F_IMF(CExtractCallback::SetTotal(UInt64)) { return CheckCancel(); }
Z7_COM7F_IMF(CExtractCallback::SetCompleted(const UInt64 *)) { return CheckCancel(); }
Z7_COM7F_IMF(CExtractCallback::GetStream(UInt32 index, ISequentialOutStream **stream, Int32 mode))
{
  *stream = NULL;
  CurrentSelected = index == Index;
  if (CurrentSelected && mode == NArchive::NExtract::NAskMode::kExtract && Output)
  {
    *stream = Output;
    (*stream)->AddRef();
  }
  return CheckCancel();
}
Z7_COM7F_IMF(CExtractCallback::PrepareOperation(Int32)) { return CheckCancel(); }
Z7_COM7F_IMF(CExtractCallback::SetOperationResult(Int32 result))
{
  if (result != NArchive::NExtract::NOperationResult::kOK) Failed = true;
  if (CurrentSelected) Completed = true;
  return CheckCancel();
}
Z7_COM7F_IMF(CExtractCallback::CryptoGetTextPassword(BSTR *password))
{
  *password = NULL;
  return HasPassword ? StringToBstr(Password.c_str(), password) : E_ACCESSDENIED;
}

struct CLibrary
{
  HMODULE Handle = NULL;
  ~CLibrary() { if (Handle) FreeLibrary(Handle); }
};

struct CLog
{
  FILE *File = NULL;
  ~CLog() { if (File) fclose(File); }
};

struct COutput
{
  CMyComPtr<ISequentialOutStream> Stream;
  COutFileStream *Spec = NULL;
  std::wstring Path;
  bool Created = false;
  bool Success = false;
  ~COutput()
  {
    if (Spec) Spec->Close();
    Stream.Release();
    if (Created && !Success) DeleteFileW(Path.c_str());
  }
};

bool Unsigned(const std::wstring &text, UInt32 &value)
{
  if (text.empty()) return false;
  UInt64 n = 0;
  for (wchar_t c : text)
  {
    if (c < L'0' || c > L'9') return false;
    n = n * 10 + static_cast<unsigned>(c - L'0');
    if (n > 0xffffffff) return false;
  }
  value = static_cast<UInt32>(n);
  return true;
}

HRESULT StreamCheck(IInStream *stream)
{
  CMyComPtr<IStreamGetSize> getSize;
  if (stream->QueryInterface(IID_IStreamGetSize, reinterpret_cast<void **>(&getSize)) != S_OK)
    return E_FAIL;
  UInt64 size = 0, pos = 0;
  UInt32 got = 99;
  unsigned char data[32];
  if (getSize->GetSize(&size) != S_OK || size < 32 ||
      stream->Read(NULL, 0, &got) != S_OK || got != 0 ||
      stream->Seek(-1, STREAM_SEEK_SET, NULL) == S_OK ||
      stream->Seek(0, 99, NULL) == S_OK ||
      stream->Seek(9, STREAM_SEEK_END, &pos) != S_OK || pos != size + 9 ||
      stream->Read(data, sizeof(data), &got) != S_OK || got != 0 ||
      stream->Seek((std::numeric_limits<Int64>::max)(), STREAM_SEEK_SET, NULL) != S_OK ||
      stream->Seek(1, STREAM_SEEK_CUR, NULL) == S_OK ||
      stream->Seek((std::numeric_limits<Int64>::min)(), STREAM_SEEK_CUR, NULL) == S_OK)
    return E_FAIL;
  const UInt64 positions[] = {0, 262140, size - 32, size > (UInt64(1) << 32) ? (UInt64(1) << 32) + 3 : 16};
  for (UInt64 at : positions)
  {
    if (at + sizeof(data) > size) continue;
    if (stream->Seek(static_cast<Int64>(at), STREAM_SEEK_SET, &pos) != S_OK || pos != at) return E_FAIL;
    unsigned done = 0;
    while (done < sizeof(data))
    {
      if (stream->Read(data + done, static_cast<UInt32>(sizeof(data) - done), &got) != S_OK || !got)
        return E_FAIL;
      done += got;
    }
    for (unsigned i = 0; i < sizeof(data); ++i)
      if (data[i] != (at + i) % 251) return E_FAIL;
  }
  return S_OK;
}

int Run(int argc, wchar_t **argv)
{
  if (argc < 4)
  {
    fputs("7zremote l|x|t zip|7z SOURCE --dll ABSOLUTE_7Z_DLL [--trace CSV]\n"
          "  x/t require --index N; x also requires --out NEW_FILE.\n"
          "  Optional: --password-env NAME, --timeout-ms N, --cancel-after-ms N.\n"
          "  streamcheck raw URL tests a byte-pattern fixture without a DLL.\n", stderr);
    return 2;
  }
  const std::wstring command = argv[1], format = argv[2], source = argv[3];
  std::wstring dll, trace, out, password;
  bool hasPassword = false, hasIndex = false;
  UInt32 index = 0, timeout = 3000, cancelAfter = 0;
  bool useTimer = false;
  for (int i = 4; i < argc; i += 2)
  {
    if (i + 1 == argc) throw std::runtime_error("Missing option value.");
    const std::wstring key = argv[i], value = argv[i + 1];
    if (key == L"--dll") dll = value;
    else if (key == L"--trace") trace = value;
    else if (key == L"--out") out = value;
    else if (key == L"--index") { hasIndex = Unsigned(value, index); if (!hasIndex) return 2; }
    else if (key == L"--timeout-ms") { if (!Unsigned(value, timeout) || !timeout || timeout > 60000) return 2; }
    else if (key == L"--cancel-after-ms") { if (!Unsigned(value, cancelAfter)) return 2; useTimer = true; }
    else if (key == L"--password-env")
    {
      const wchar_t *p = _wgetenv(value.c_str());
      if (!p) throw std::runtime_error("Password environment variable is not set.");
      password = p;
      hasPassword = true;
    }
    else throw std::runtime_error("Unknown option.");
  }
  if (command != L"l" && command != L"x" && command != L"t" && command != L"streamcheck") return 2;
  if (command != L"streamcheck" && (format != L"zip" && format != L"7z")) return 2;
  if ((command == L"x" || command == L"t") && !hasIndex) return 2;
  if (command == L"x" && out.empty()) return 2;

  CCancelScope cancel;
  if (useTimer) { if (!cancelAfter) SetEvent(cancel.Event); else cancel.After(cancelAfter); }
  CLibrary library; // Must outlive all engine objects.
  Func_CreateObject create = NULL;
  if (command != L"streamcheck")
  {
    // The caller explicitly selects the DLL; never search CWD for executable code.
    if (dll.size() < 3 || dll[1] != L':' || (dll[2] != L'\\' && dll[2] != L'/'))
      throw std::runtime_error("--dll must be an absolute drive path to a trusted 7z.dll.");
    library.Handle = LoadLibraryExW(dll.c_str(), NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library.Handle) throw std::runtime_error("Cannot load the selected 7z.dll.");
    create = reinterpret_cast<Func_CreateObject>(GetProcAddress(library.Handle, "CreateObject"));
    if (!create) throw std::runtime_error("DLL does not export CreateObject.");
  }
  CLog log;
  if (!trace.empty())
  {
    log.File = _wfopen(trace.c_str(), L"wb");
    if (!log.File) throw std::runtime_error("Cannot create trace file.");
  }
  CMyComPtr<IInStream> backing;
  CHttpInStream *remote = NULL;
  HRESULT result;
  const bool isHttp = _wcsnicmp(source.c_str(), L"https://", 8) == 0 ||
      _wcsnicmp(source.c_str(), L"http://", 7) == 0;
  if (isHttp)
  {
    remote = new CHttpInStream;
    backing = remote;
    NWindows::CHttpRangeOptions options;
    options.CancelEvent = cancel.Event;
    options.TimeoutMs = static_cast<int>(timeout);
    result = remote->Open(source.c_str(), options);
  }
  else
  {
    CInFileStream *local = new CInFileStream;
    backing = local;
    result = local->Open(source.c_str()) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
  }
  CTracingInStream *tracer = new CTracingInStream(backing, log.File);
  CMyComPtr<IInStream> stream = tracer;
  CMyComPtr<IInArchive> archive;
  COutput output;
  if (result == S_OK && command == L"streamcheck") result = StreamCheck(stream);
  else if (result == S_OK)
  {
    GUID clsid = {0x23170f69, 0x40c1, 0x278a, {0x10, 0, 0, 1, 0x10, 0, 0, 0}};
    clsid.Data4[5] = format == L"zip" ? 1 : 7;
    result = create(&clsid, &IID_IInArchive, reinterpret_cast<void **>(&archive));
    if (result == S_OK)
    {
      COpenCallback *open = new COpenCallback;
      CMyComPtr<IArchiveOpenCallback> callback = open;
      open->Password = password;
      open->HasPassword = hasPassword;
      const UInt64 scanLimit = 0; // Prototype intentionally excludes SFX scanning.
      result = archive->Open(stream, &scanLimit, callback);
    }
    UInt32 count = 0;
    if (result == S_OK) result = archive->GetNumberOfItems(&count);
    if (result == S_OK && command == L"l")
    {
      tracer->Phase = "list";
      for (UInt32 i = 0; i < count && result == S_OK; ++i)
      {
        CProperty path, size, directory;
        result = archive->GetProperty(i, kpidPath, &path.Value);
        if (result != S_OK) break;
        result = archive->GetProperty(i, kpidSize, &size.Value);
        if (result != S_OK) break;
        result = archive->GetProperty(i, kpidIsDir, &directory.Value);
        if (result != S_OK) break;
        const std::wstring name = path.Value.vt == VT_BSTR ?
            std::wstring(path.Value.bstrVal, SysStringLen(path.Value.bstrVal)) : L"";
        printf("{\"index\":%u,\"path\":%s,\"size\":%llu,\"directory\":%s}\n", i,
            Json(Utf8(name)).c_str(), static_cast<unsigned long long>(
              size.Value.vt == VT_UI8 ? size.Value.uhVal.QuadPart : 0),
            directory.Value.vt == VT_BOOL && directory.Value.boolVal ? "true" : "false");
      }
    }
    else if (result == S_OK)
    {
      if (index >= count) result = E_INVALIDARG;
      CProperty directory;
      if (result == S_OK) result = archive->GetProperty(index, kpidIsDir, &directory.Value);
      if (result == S_OK && directory.Value.vt == VT_BOOL && directory.Value.boolVal) result = E_INVALIDARG;
      if (result == S_OK && command == L"x")
      {
        output.Spec = new COutFileStream;
        output.Stream = output.Spec;
        output.Path = out;
        output.Created = output.Spec->Create_NEW(out.c_str());
        if (!output.Created) result = HRESULT_FROM_WIN32(GetLastError());
      }
      if (result == S_OK)
      {
        CExtractCallback *extract = new CExtractCallback;
        CMyComPtr<IArchiveExtractCallback> callback = extract;
        extract->Index = index;
        extract->Output = output.Stream;
        extract->Password = password;
        extract->HasPassword = hasPassword;
        tracer->Phase = "extract";
        result = archive->Extract(&index, 1, command == L"t", callback);
        if (result == S_OK && (!extract->Completed || extract->Failed)) result = E_FAIL;
      }
      if (output.Spec)
      {
        const HRESULT closed = output.Spec->Close();
        if (result == S_OK) result = closed;
      }
      output.Success = result == S_OK;
    }
    if (archive) archive->Close();
  }
  if (log.File && fflush(log.File) != 0 && result == S_OK) result = E_FAIL;
  fprintf(stderr, "{\"result\":\"%08lx\",\"read_calls\":%llu,\"read_bytes\":%llu,"
      "\"requests\":%llu,\"downloaded\":%llu,\"source_size\":%llu,\"cache_hits\":%llu,"
      "\"http_status\":%u,\"error\":%s}\n", static_cast<unsigned long>(result),
      static_cast<unsigned long long>(tracer->ReadCalls), static_cast<unsigned long long>(tracer->ReadBytes),
      static_cast<unsigned long long>(remote ? remote->Source.Requests : 0),
      static_cast<unsigned long long>(remote ? remote->Source.Downloaded : 0),
      static_cast<unsigned long long>(remote ? remote->Source.Size : 0),
      static_cast<unsigned long long>(remote ? remote->CacheHits : 0),
      remote ? remote->Source.Status : 0, Json(remote ? remote->Source.Error : "").c_str());
  return result == S_OK ? 0 : 1;
}
}

int wmain(int argc, wchar_t **argv)
{
  try { return Run(argc, argv); }
  catch (const std::exception &e) { fprintf(stderr, "Error: %s\n", e.what()); return 1; }
  catch (...) { fputs("Unexpected failure.\n", stderr); return 1; }
}
