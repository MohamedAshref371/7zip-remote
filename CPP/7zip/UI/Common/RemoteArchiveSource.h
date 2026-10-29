// Optional Windows HTTP source for read-only archive operations.
#ifndef ZIP7_INC_REMOTE_ARCHIVE_SOURCE_H
#define ZIP7_INC_REMOTE_ARCHIVE_SOURCE_H

#ifdef Z7_HTTP
#include "../../Common/HttpInStream.h"
#include "ArchiveOpenCallback.h"

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

  HRESULT Open(const UString &url, IOpenCallbackUI *callback)
  {
    NWindows::CHttpRangeOptions options;
    options.CancelCheck = CheckBreak;
    options.CancelContext = callback;
    return Open(url, options);
  }
};
#endif
#endif
