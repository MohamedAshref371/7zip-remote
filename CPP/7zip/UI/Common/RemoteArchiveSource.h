// Optional Windows HTTP source for read-only archive operations.
#ifndef ZIP7_INC_REMOTE_ARCHIVE_SOURCE_H
#define ZIP7_INC_REMOTE_ARCHIVE_SOURCE_H

#ifdef Z7_HTTP
#include "../../Common/HttpInStream.h"
#include "ArchiveOpenCallback.h"
#include "RemoteProgress.h"
#include "../../Archive/IArchiveReadRange.h"

inline bool IsHttpArchiveUrl(const UString &path)
{
  return path.IsPrefixedBy_Ascii_NoCase("http://")
      || path.IsPrefixedBy_Ascii_NoCase("https://");
}

class CRemoteArchiveSource
{
  static bool CheckBreak(void *context)
  {
    return context && static_cast<IOpenCallbackUI *>(context)->Open_CheckBreak() != S_OK;
  }
public:
  CMyComPtr<IInStream> Stream;
  CHttpInStream *Spec;
  UString DisplayName;

  CRemoteArchiveSource(): Spec(NULL) {}

  void Reset()
  {
    Stream.Release();
    Spec = NULL;
    DisplayName.Empty();
  }

  AString ErrorMessage() const
  {
    AString message(Spec->Source.Error.c_str());
    if (Spec->Source.Status)
    {
      message += " (HTTP ";
      message.Add_UInt32(Spec->Source.Status);
      message += ')';
    }
    return message;
  }

  static UString DisplayNameFromUrl(const UString &url)
  {
    // Never use a URL as a local path or expose signed query/userinfo in output.
    // Keep only a conservative filename hint; the engine verifies the format.
    UString name("remote-archive");
    unsigned end = url.Len();
    for (unsigned i = 0; i < url.Len(); ++i)
      if (url[i] == L'?' || url[i] == L'#') { end = i; break; }
    unsigned start = end;
    while (start && url[start - 1] != L'/') --start;
    if (start < end && end - start <= 160)
    {
      name.Empty();
      for (unsigned i = start; i < end; ++i)
      {
        const wchar_t c = url[i];
        name += ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')
            || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.') ? c : L'_';
      }
      if (name == L"." || name == L"..")
        name = "remote-archive";
    }
    return name;
  }

  HRESULT Open(const UString &url, const NWindows::CHttpRangeOptions &options)
  {
    DisplayName = DisplayNameFromUrl(url);
    Spec = new CHttpInStream;
    Stream = Spec;
    return Spec->Open(url, options);
  }

  HRESULT Open(const UString &url, IOpenCallbackUI *callback, bool skipETagVerification = false)
  {
    NWindows::CHttpRangeOptions options;
    options.CancelCheck = CheckBreak;
    options.CancelContext = callback;
    options.SkipETagVerification = skipETagVerification;
    return Open(url, options);
  }
};

// Keep callbacks alive only for the operation, never for the browsing session.
class CRemoteReadRangeSink Z7_final: public IArchiveReadRangeSink, public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1(IArchiveReadRangeSink)
public:
  std::vector<CHttpReadRange> Ranges;
  Z7_COM7F_IMF(AddReadRange(UInt64 start, UInt64 length)) Z7_override
  {
    try
    {
      CHttpReadRange range = {start, length};
      Ranges.push_back(range);
      return S_OK;
    }
    catch (...) { return E_OUTOFMEMORY; }
  }
};

class CRemoteProgressScope
{
  CRemoteArchiveSource &_source;
  CMyComPtr<IRemoteProgress> _callback;
  UInt64 _bytesBase;
  UInt64 _requestsBase;
  bool _estimateDefined;
  UInt64 _estimate;
  static void Report(void *context, ULONGLONG bytes, ULONGLONG size, ULONGLONG requests)
  {
    CRemoteProgressScope &self = *static_cast<CRemoteProgressScope *>(context);
    if (self._callback)
    {
      self._callback->SetNetworkProgress(bytes - self._bytesBase, size, requests - self._requestsBase);
      self._callback->SetNetworkEstimate(self._estimate, self._estimateDefined);
    }
  }
public:
  CRemoteProgressScope(CRemoteArchiveSource &source, IUnknown *callback):
      _source(source), _bytesBase(0), _requestsBase(0), _estimateDefined(false), _estimate(0)
  {
    if (callback) callback->QueryInterface(IID_IRemoteProgress, (void **)&_callback);
    if (source.Spec)
    {
      _bytesBase = source.Spec->Source.Downloaded;
      _requestsBase = source.Spec->Source.Requests;
      source.Spec->Source.SetProgress(Report, this);
    }
  }
  void Configure(NWindows::CHttpRangeOptions &options)
  {
    options.Progress = Report;
    options.ProgressContext = this;
  }
  void Estimate(IInArchive *archive, const UInt32 *indices, UInt32 count)
  {
    if (!_source.Spec || !_callback || !archive) return;
    // Failure to estimate must never prevent extraction or trigger network I/O.
    try
    {
      CMyComPtr<IArchiveReadRange> plan;
      CMyComPtr<IArchiveReadRanges> multiPlan;
      archive->QueryInterface(IID_IArchiveReadRange, (void **)&plan);
      archive->QueryInterface(IID_IArchiveReadRanges, (void **)&multiPlan);
      if (!plan && !multiPlan) return;
      CRemoteReadRangeSink *sink = new CRemoteReadRangeSink;
      CMyComPtr<IArchiveReadRangeSink> sinkRef = sink;
      sink->Ranges.reserve(count);
      for (UInt32 i = 0; i < count; ++i)
      {
        if (multiPlan)
        {
          if (multiPlan->GetItemReadRanges(indices[i], sinkRef) != S_OK) return;
        }
        else
        {
          CHttpReadRange range;
          if (plan->GetItemReadRange(indices[i], &range.Start, &range.Size) != S_OK) return;
          if (sink->AddReadRange(range.Start, range.Size) != S_OK) return;
        }
      }
      _estimateDefined = _source.Spec->EstimateMissingBytes(sink->Ranges, _estimate);
      _callback->SetNetworkEstimate(_estimate, _estimateDefined);
    }
    catch (...) { /* Optional estimate unavailable; normal extraction continues. */ }
  }
  ~CRemoteProgressScope()
  {
    if (_source.Spec) _source.Spec->Source.SetProgress(NULL, NULL);
  }
private:
  CRemoteProgressScope(const CRemoteProgressScope &);
  CRemoteProgressScope &operator=(const CRemoteProgressScope &);
};

// Extraction callbacks, like opening callbacks, belong to a single operation.
class CRemoteExtractCancelScope
{
  CRemoteArchiveSource &_source;
  CMyComPtr<IProgress> _callback;
  static bool Check(void *context)
  {
    CRemoteExtractCancelScope &self = *static_cast<CRemoteExtractCancelScope *>(context);
    return self._callback && self._callback->SetCompleted(NULL) != S_OK;
  }
public:
  CRemoteExtractCancelScope(CRemoteArchiveSource &source, IProgress *callback):
      _source(source), _callback(callback)
  {
    if (source.Spec) source.Spec->Source.SetCancelCheck(Check, this);
  }
  ~CRemoteExtractCancelScope()
  {
    if (_source.Spec) _source.Spec->Source.SetCancelCheck(NULL, NULL);
  }
private:
  CRemoteExtractCancelScope(const CRemoteExtractCancelScope &);
  CRemoteExtractCancelScope &operator=(const CRemoteExtractCancelScope &);
};
#endif
#endif
