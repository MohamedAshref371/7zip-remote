#include "StdAfx.h"
#include "HttpRangeSource.h"

#include <winhttp.h>
#include <algorithm>
#include <limits>

namespace NWindows {
namespace {
struct CRequestHandle
{
  HINTERNET Value;
  HANDLE Completed;
  HANDLE Closed;
  DWORD Error;
  DWORD ReadSize;
  bool Watching;
  explicit CRequestHandle(HINTERNET value): Value(value),
      Completed(value ? CreateEventW(NULL, TRUE, FALSE, NULL) : NULL), Closed(NULL),
      Error(0), ReadSize(0), Watching(false)
  {
    if (Completed) Closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Completed || !Closed) Error = GetLastError();
  }
  static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void *info, DWORD length)
  {
    if (!context) return;
    CRequestHandle &self = *reinterpret_cast<CRequestHandle *>(context);
    switch (status)
    {
      case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        SetEvent(self.Closed); // Last notification; do not touch self afterwards.
        return;
      case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        self.Error = length >= sizeof(WINHTTP_ASYNC_RESULT)
            ? static_cast<WINHTTP_ASYNC_RESULT *>(info)->dwError : ERROR_GEN_FAILURE;
        break;
      case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        self.ReadSize = length;
        break;
      case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
      case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        break;
      default:
        return;
    }
    SetEvent(self.Completed);
  }
  bool Initialize()
  {
    if (!Completed || !Closed) { SetLastError(Error); return false; }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
    if (!WinHttpSetOption(Value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return false;
    if (WinHttpSetStatusCallback(Value, Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS
        | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) return false;
    Watching = true;
    return true;
  }
  void Prepare()
  {
    ResetEvent(Completed);
    Error = ReadSize = 0;
  }
  DWORD Wait(const CHttpRangeSource &source, DWORD begun, unsigned deadline)
  {
    for (;;)
    {
      // Only the operation's original thread invokes application callbacks.
      if (source.IsCancelled()) return ERROR_CANCELLED;
      const DWORD elapsed = GetTickCount() - begun;
      if (elapsed >= deadline) return ERROR_TIMEOUT;
      const DWORD result = WaitForSingleObject(Completed, (std::min<DWORD>)(50, deadline - elapsed));
      if (result == WAIT_OBJECT_0) return source.IsCancelled() ? ERROR_CANCELLED : Error;
      if (result == WAIT_FAILED) return GetLastError();
    }
  }
  ~CRequestHandle()
  {
    if (Value)
    {
      WinHttpCloseHandle(Value); // Abort outstanding asynchronous I/O.
      // The request context and read buffers must survive all late callbacks.
      if (Watching) WaitForSingleObject(Closed, INFINITE);
    }
    if (Completed) CloseHandle(Completed);
    if (Closed) CloseHandle(Closed);
  }
private:
  CRequestHandle(const CRequestHandle &) = delete;
  CRequestHandle &operator=(const CRequestHandle &) = delete;
};

HRESULT LastErrorResult()
{
  const DWORD error = GetLastError();
  return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

// Read one header only. Duplicate validators/ranges are ambiguous and rejected.
HRESULT Header(HINTERNET request, DWORD query, std::wstring &value)
{
  value.clear();
  DWORD bytes = 0;
  DWORD index = 0;
  if (WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
      NULL, &bytes, &index))
    return E_FAIL;
  const DWORD error = GetLastError();
  if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
    return S_FALSE;
  if (error != ERROR_INSUFFICIENT_BUFFER || bytes > 32768 || bytes < sizeof(wchar_t))
    return E_FAIL;
  std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, 0);
  index = 0;
  if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
      buffer.data(), &bytes, &index))
    return LastErrorResult();
  value.assign(buffer.data(), bytes / sizeof(wchar_t));
  DWORD extra = 0;
  if (WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
      NULL, &extra, &index) || GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND)
    return E_FAIL;
  return S_OK;
}

bool Number(const wchar_t *&p, ULONGLONG &value)
{
  if (*p < L'0' || *p > L'9') return false;
  value = 0;
  do
  {
    const unsigned digit = static_cast<unsigned>(*p++ - L'0');
    if (value > ((std::numeric_limits<ULONGLONG>::max)() - digit) / 10)
      return false;
    value = value * 10 + digit;
  } while (*p >= L'0' && *p <= L'9');
  return true;
}

bool ParseRange(const std::wstring &text, ULONGLONG &start,
    ULONGLONG &end, ULONGLONG &total)
{
  if (text.compare(0, 6, L"bytes ") != 0) return false;
  const wchar_t *p = text.c_str() + 6;
  if (!Number(p, start) || *p++ != L'-' || !Number(p, end) ||
      *p++ != L'/' || !Number(p, total) || *p != 0)
    return false;
  return start <= end && end < total &&
      total <= static_cast<ULONGLONG>((std::numeric_limits<LONGLONG>::max)());
}

bool StrongETag(const std::wstring &tag)
{
  if (tag.size() < 2 || tag.front() != L'"' || tag.back() != L'"') return false;
  for (size_t i = 1; i + 1 < tag.size(); ++i)
    if (tag[i] < 0x21 || tag[i] == L'"' || tag[i] == 0x7f || tag[i] > 0xff)
      return false;
  return true;
}

// Never shorten Retry-After. Unparseable/ambiguous values suppress retry.
bool RetryDelay(HINTERNET request, unsigned &delay)
{
  std::wstring value;
  const HRESULT result = Header(request, WINHTTP_QUERY_RETRY_AFTER, value);
  if (result == S_FALSE) return true;
  if (result != S_OK || value.empty()) return false;
  ULONGLONG milliseconds = 0, seconds = 0;
  const wchar_t *p = value.c_str();
  if (*p >= L'0' && *p <= L'9')
  {
    if (!Number(p, seconds) || *p || seconds > MAXDWORD / 1000) return false;
    milliseconds = seconds * 1000;
  }
  else
  {
    SYSTEMTIME time = {};
    FILETIME future, now;
    wchar_t canonical[WINHTTP_TIME_FORMAT_BUFSIZE];
    // WinHTTP's date parser is permissive even for malformed numeric strings.
    // Accept a round-trippable IMF-fixdate; conservatively stop for other forms.
    if (value.size() != 29 || !WinHttpTimeToSystemTime(value.c_str(), &time)
        || !WinHttpTimeFromSystemTime(&time, canonical) || _wcsicmp(value.c_str(), canonical) != 0
        || !SystemTimeToFileTime(&time, &future))
      return false;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG target = (static_cast<ULONGLONG>(future.dwHighDateTime) << 32) | future.dwLowDateTime;
    const ULONGLONG current = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    if (target > current) milliseconds = (target - current + 9999) / 10000;
  }
  if (milliseconds > MAXDWORD) return false;
  delay = static_cast<unsigned>(milliseconds);
  return true;
}

// RFC 3986 relative references. Never decode/re-encode signed paths or queries.
std::wstring ResolveRedirect(const std::wstring &base, std::wstring target)
{
  const size_t fragment = target.find(L'#');
  if (fragment != std::wstring::npos) target.erase(fragment);
  const size_t authorityEnd = base.find_first_of(L"/?", base.find(L"://") + 3);
  const std::wstring origin = base.substr(0, authorityEnd);
  const size_t schemeEnd = base.find(L':');
  const size_t colon = target.find(L':');
  const size_t delimiter = target.find_first_of(L"/?");
  if (colon != std::wstring::npos && (delimiter == std::wstring::npos || colon < delimiter))
    return target; // SetUrl validates the absolute scheme and authority.
  if (target.compare(0, 2, L"//") == 0)
    return base.substr(0, schemeEnd + 1) + target;
  const std::wstring basePath = authorityEnd == std::wstring::npos || base[authorityEnd] == L'?'
      ? L"/" : base.substr(authorityEnd, base.find(L'?', authorityEnd) - authorityEnd);
  if (target.empty()) return base;
  if (target[0] == L'?') return origin + basePath + target;
  std::wstring path = target[0] == L'/' ? target
      : basePath.substr(0, basePath.rfind(L'/') + 1) + target;
  const size_t query = path.find(L'?');
  const std::wstring suffix = query == std::wstring::npos ? L"" : path.substr(query);
  if (query != std::wstring::npos) path.erase(query);
  // Remove literal dot segments only; preserve escaped dots and empty segments.
  std::wstring output;
  while (!path.empty())
  {
    if (path.compare(0, 3, L"../") == 0) path.erase(0, 3);
    else if (path.compare(0, 2, L"./") == 0) path.erase(0, 2);
    else if (path.compare(0, 3, L"/./") == 0 || path == L"/.") path.replace(0, 2, L"");
    else if (path.compare(0, 4, L"/../") == 0 || path == L"/..")
    {
      path.replace(0, 3, L"");
      const size_t slash = output.rfind(L'/');
      output.erase(slash == std::wstring::npos ? 0 : slash);
    }
    else if (path == L"." || path == L"..") path.clear();
    else
    {
      const size_t next = path.find(L'/', path[0] == L'/' ? 1 : 0);
      output.append(path, 0, next);
      path.erase(0, next == std::wstring::npos ? path.size() : next);
    }
  }
  return origin + output + suffix;
}
}

CHttpRangeSource::~CHttpRangeSource()
{
  if (_connection) WinHttpCloseHandle(_connection);
  if (_session) WinHttpCloseHandle(_session);
}

bool CHttpRangeSource::IsCancelled() const
{
  return (_options.CancelEvent && WaitForSingleObject(_options.CancelEvent, 0) == WAIT_OBJECT_0)
      || (_options.CancelCheck && _options.CancelCheck(_options.CancelContext));
}

HRESULT CHttpRangeSource::Fail(const char *message, HRESULT result)
{
  Error = message; // Deliberately excludes URL, query tokens and response body.
  _ready = false;
  return result;
}

void CHttpRangeSource::ReportProgress() const
{
  if (_options.Progress)
    _options.Progress(_options.ProgressContext, Downloaded, Size, Requests);
}

HRESULT CHttpRangeSource::FailNetwork()
{
  const DWORD error = GetLastError();
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  _retryable = error == ERROR_WINHTTP_TIMEOUT || error == ERROR_WINHTTP_CANNOT_CONNECT
      || error == ERROR_WINHTTP_CONNECTION_ERROR || error == ERROR_WINHTTP_RESEND_REQUEST
      || error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
  const char *message = "Network request failed. Check your connection and reopen the archive.";
  switch (error)
  {
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_TIMEOUT:
      message = "The server took too long to respond. Check your connection and reopen the archive.";
      break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      message = "Cannot resolve the server name. Check the URL and your network connection.";
      break;
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
      message = "Cannot connect to the server or the connection was interrupted. Check your connection and reopen the archive.";
      break;
    case ERROR_WINHTTP_SECURE_FAILURE:
      message = "HTTPS certificate verification failed. Check the server certificate and your system date.";
      break;
  }
  return Fail(message, HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE));
}

HRESULT CHttpRangeSource::Open(const wchar_t *url, const CHttpRangeOptions &options)
{
  if (_session) return Fail("Create a new source to reopen an archive.", E_UNEXPECTED);
  _options = options;
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  if (!url || options.TimeoutMs <= 0 || options.RequestDeadlineMs == 0)
    return Fail("Invalid source options.", E_INVALIDARG);
  _session = WinHttpOpen(L"7-Zip-Range-Prototype/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
  if (!_session) return Fail("Cannot create HTTP session.", LastErrorResult());
  if (!WinHttpSetTimeouts(_session, options.TimeoutMs, options.TimeoutMs,
      options.TimeoutMs, options.TimeoutMs))
    return Fail("Cannot configure HTTP timeouts.", LastErrorResult());
  HRESULT result = SetUrl(url);
  if (result != S_OK) return result;
  std::vector<unsigned char> probe;
  result = Request(0, 1, true, probe);
  _ready = result == S_OK;
  return result;
}

HRESULT CHttpRangeSource::SetUrl(const std::wstring &url)
{
  // Apply the same validation to initial and redirected URLs.
  if (url.size() > 32768) return Fail("HTTP URL is too long.", E_INVALIDARG);
  for (const wchar_t c : url)
    if (c <= 0x20 || c > 0x7e || c == L'\\' || c == L'#')
      return Fail("Use an ASCII percent-encoded HTTP(S) URL without a fragment.", E_INVALIDARG);
  URL_COMPONENTS parts = {};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
  const size_t authorityStart = url.find(L"://");
  const size_t authorityEnd = authorityStart == std::wstring::npos ? 0
      : url.find_first_of(L"/?", authorityStart + 3);
  const size_t userinfo = url.find(L'@', authorityStart == std::wstring::npos ? 0 : authorityStart + 3);
  if (userinfo != std::wstring::npos && (authorityEnd == std::wstring::npos || userinfo < authorityEnd))
    return Fail("Expected an HTTP(S) URL without embedded credentials.", E_INVALIDARG);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || !parts.dwHostNameLength ||
      parts.dwUserNameLength || parts.dwPasswordLength ||
      (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
    return Fail("Expected an HTTP(S) URL without embedded credentials.", E_INVALIDARG);
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  if (_secure && !secure) return Fail("Redirect from HTTPS to HTTP is not allowed.");
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  _path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (_path.empty()) _path = L"/";
  if (parts.dwExtraInfoLength) _path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (_connection) { WinHttpCloseHandle(_connection); _connection = NULL; }
  _connection = WinHttpConnect(_session, host.c_str(), parts.nPort, 0);
  if (!_connection) return Fail("Cannot create HTTP connection.", LastErrorResult());
  _url = url;
  _secure = secure;
  return S_OK;
}

HRESULT CHttpRangeSource::Fetch(ULONGLONG start, unsigned length,
    std::vector<unsigned char> &data)
{
  if (!_ready) return E_UNEXPECTED;
  if (!length || start >= Size || length > Size - start)
    return Fail("Invalid requested byte range.", E_INVALIDARG);
  return Request(start, length, false, data);
}

HRESULT CHttpRangeSource::Request(ULONGLONG start, unsigned length, bool probe,
    std::vector<unsigned char> &data)
{
  const DWORD begun = GetTickCount();
  unsigned hops = 0, retries = 0;
  for (;;)
  {
    std::wstring redirect;
    _retryable = false;
    _retryDelayMs = 0;
    const HRESULT result = RequestOnce(start, length, probe, data, begun, redirect);
    if (result == S_OK)
    {
      _ready = true;
      Error.clear();
      return S_OK;
    }
    if (result == S_FALSE)
    {
      if (hops++ == 5) return Fail("Too many HTTP redirects (maximum 5). Obtain a fresh direct download URL.");
      if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
      const HRESULT changed = SetUrl(ResolveRedirect(_url, redirect));
      if (changed != S_OK) return changed;
      continue;
    }
    if (result == E_ABORT || !_retryable || retries == 2) return result;
    const unsigned delay = (std::max)(_retryDelayMs, 250u << retries);
    const DWORD elapsed = GetTickCount() - begun;
    // Preserve the original failure if the server's wait will not fit the budget.
    if (elapsed >= _options.RequestDeadlineMs || delay >= _options.RequestDeadlineMs - elapsed)
      return result;
    ++retries;
    const DWORD waiting = GetTickCount();
    for (;;)
    {
      if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
      const DWORD waited = GetTickCount() - waiting;
      if (waited >= delay) break;
      const DWORD spent = GetTickCount() - begun;
      if (spent >= _options.RequestDeadlineMs)
        return Fail("HTTP request deadline exceeded. Check your connection and reopen the archive.", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
      const DWORD remaining = delay - waited;
      const DWORD slice = (std::min<DWORD>)(50, (std::min<DWORD>)(remaining, _options.RequestDeadlineMs - spent));
      if (_options.CancelEvent) WaitForSingleObject(_options.CancelEvent, slice);
      else Sleep(slice);
    }
    // RequestOnce has already closed/drained failed I/O and discarded its body.
    // Restart the same full range; never combine bytes from incomplete attempts.
  }
}

HRESULT CHttpRangeSource::RequestOnce(ULONGLONG start, unsigned length, bool probe,
    std::vector<unsigned char> &data, DWORD begun, std::wstring &redirect)
{
  data.clear();
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  if (static_cast<DWORD>(GetTickCount() - begun) >= _options.RequestDeadlineMs)
    return Fail("HTTP request deadline exceeded. Check your connection and reopen the archive.", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
  // Declare I/O buffers before the request so its destructor cancels/drains
  // callbacks before cancellation can release any buffer still used by WinHTTP.
  std::vector<unsigned char> body;
  unsigned char extra;
  const std::wstring headers = L"Accept-Encoding: identity\r\nRange: bytes=" +
      std::to_wstring(start) + L"-" + std::to_wstring(start + length - 1) + L"\r\n" +
      (probe || _options.SkipETagVerification ? L"" : L"If-Match: " + _etag + L"\r\n");
  CRequestHandle request(WinHttpOpenRequest(_connection, L"GET", _path.c_str(), NULL,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      (_secure ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_ESCAPE_DISABLE));
  if (!request.Value) return Fail("Cannot create HTTP request.", LastErrorResult());
  if (!request.Initialize()) return Fail("Cannot configure asynchronous HTTP request.", LastErrorResult());
  const auto wait = [&](BOOL started) -> HRESULT
  {
    if (!started) return FailNetwork();
    const DWORD error = request.Wait(*this, begun, _options.RequestDeadlineMs);
    if (error == ERROR_CANCELLED) return Fail("Cancelled.", E_ABORT);
    if (error)
    {
      SetLastError(error);
      return FailNetwork();
    }
    return S_OK;
  };
  // Preserve the response timeout as well as the complete-request deadline.
  DWORD responseTimeout = (std::min)(static_cast<unsigned>(_options.TimeoutMs),
      _options.RequestDeadlineMs);
  if (!WinHttpSetTimeouts(request.Value, _options.TimeoutMs, _options.TimeoutMs,
      _options.TimeoutMs, _options.TimeoutMs) ||
      !WinHttpSetOption(request.Value, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
          &responseTimeout, sizeof(responseTimeout)))
    return Fail("Cannot configure request timeouts.", LastErrorResult());
  // Handle redirects on the caller thread, with a shared deadline and hop limit.
  // No cookies or automatic server/proxy authentication, including on new hosts.
  DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
  if (!WinHttpSetOption(request.Value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
    return Fail("Cannot configure HTTP request policy.", LastErrorResult());
  ++Requests;
  ReportProgress();
  Status = 0; // A failed request must not report the previous response's status.
  request.Prepare();
  HRESULT pending = wait(WinHttpSendRequest(request.Value, headers.c_str(), static_cast<DWORD>(headers.size()),
      WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)));
  if (pending != S_OK) return pending;
  request.Prepare();
  pending = wait(WinHttpReceiveResponse(request.Value, NULL));
  if (pending != S_OK) return pending;
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  DWORD status = 0, statusSize = sizeof(status);
  if (!WinHttpQueryHeaders(request.Value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    return Fail("Cannot read HTTP status.", LastErrorResult());
  Status = status;
  if (status == 408 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504)
    _retryable = RetryDelay(request.Value, _retryDelayMs);
  if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
  {
    if (Header(request.Value, WINHTTP_QUERY_LOCATION, redirect) != S_OK || redirect.empty())
      return Fail("Missing, duplicate or invalid HTTP redirect Location.");
    return S_FALSE; // Request closes/drains before changing the connection.
  }
  if (status == 200) return Fail("The server does not support partial downloads for this URL. Download the archive locally to open it.");
  if (status == 401) return Fail("The server requires authentication. Browser login sessions are not supported; use a direct download URL that grants access.");
  if (status == 403) return Fail("The server denied access. The download link may have expired; obtain a fresh direct download URL and check access permissions.");
  if (status == 404 || status == 410) return Fail("The archive was not found or is no longer available. Check the URL or obtain a fresh download link.");
  if (status == 407) return Fail("The proxy requires authentication. Check your network proxy settings; proxy authentication is not supported.");
  if (status == 408) return Fail("The server timed out processing the request. Try reopening the archive later.");
  if (status == 412) return Fail("Archive changed on the server. Reopen the archive to read the current version.");
  if (status == 416) return Fail("The server rejected the requested byte range. Reopen the archive; if the problem persists, download it locally.");
  if (status == 429) return Fail("The server is limiting download requests. Wait before reopening the archive.");
  if (status >= 500 && status < 600) return Fail("The server reported a temporary failure. Try reopening the archive later.");
  if (status >= 300 && status < 400) return Fail("Unsupported HTTP redirect status. Supply a direct download URL.");
  if (status != 206)
  {
    const std::string message = "Unexpected HTTP status " + std::to_string(status)
        + ". Use a direct archive download URL that supports partial downloads.";
    return Fail(message.c_str());
  }

  std::wstring range, tag, encoding, contentLength, contentType;
  if (_options.SkipETagVerification &&
      (Header(request.Value, WINHTTP_QUERY_ETAG, tag) != S_OK || !StrongETag(tag))) tag.clear();
  ULONGLONG first = 0, last = 0, total = 0;
  if (Header(request.Value, WINHTTP_QUERY_CONTENT_RANGE, range) != S_OK ||
      !ParseRange(range, first, last, total) || first != start || last - first + 1 != length)
    return Fail("Invalid or mismatched Content-Range.");
  if (!_options.SkipETagVerification &&
      (Header(request.Value, WINHTTP_QUERY_ETAG, tag) != S_OK || !StrongETag(tag)))
    return Fail("The server did not provide a valid strong ETag to verify the archive version. Download it locally, or skip ETag verification only if you trust the file to remain unchanged (Open URL: Skip ETag verification; console: -shttp-no-etag).");
  if (!probe && (total != Size || (!_options.SkipETagVerification && tag != _etag)))
    return Fail("Archive identity or size changed during the session. Reopen the archive to read the current version.");
  HRESULT h = Header(request.Value, WINHTTP_QUERY_CONTENT_ENCODING, encoding);
  if (FAILED(h) || (h == S_OK && _wcsicmp(encoding.c_str(), L"identity") != 0))
    return Fail("Encoded HTTP representations are not supported for byte ranges.");
  h = Header(request.Value, WINHTTP_QUERY_CONTENT_TYPE, contentType);
  if (FAILED(h) || (h == S_OK && _wcsnicmp(contentType.c_str(), L"multipart/", 10) == 0))
    return Fail("Multipart range responses are not supported.");
  h = Header(request.Value, WINHTTP_QUERY_CONTENT_LENGTH, contentLength);
  if (FAILED(h)) return Fail("Invalid Content-Length headers.");
  if (h == S_OK)
  {
    ULONGLONG count = 0;
    const wchar_t *p = contentLength.c_str();
    if (!Number(p, count) || *p || count != length)
      return Fail("Content-Length does not match the requested range.");
  }
  body.resize(length);
  unsigned done = 0;
  for (;;)
  {
    if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
    // Unsigned subtraction also handles the 32-bit tick counter wrapping.
    if (static_cast<DWORD>(GetTickCount() - begun) >= _options.RequestDeadlineMs)
      return Fail("HTTP request deadline exceeded. Check your connection and reopen the archive.", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    const DWORD want = done < length ? (std::min)(length - done, 65536u) : 1;
    request.Prepare();
    pending = wait(WinHttpReadData(request.Value, done < length ? body.data() + done : &extra, want, NULL));
    if (pending != S_OK) return pending;
    const DWORD got = request.ReadSize;
    Downloaded += got;
    ReportProgress();
    if (!got) break;
    if (done == length) return Fail("Range body exceeds its declared length.");
    done += got;
  }
  if (done != length)
  {
    _retryable = true;
    return Fail("The server sent an incomplete byte range. Check your connection and reopen the archive.");
  }
  if (probe) { Size = total; _etag = tag; }
  ReportProgress();
  data.swap(body); // Publish only a complete, validated range.
  return S_OK;
}
}
