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
  explicit CRequestHandle(HINTERNET value): Value(value) {}
  ~CRequestHandle() { if (Value) WinHttpCloseHandle(Value); }
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

HRESULT CHttpRangeSource::Open(const wchar_t *url, const CHttpRangeOptions &options)
{
  if (_session) return Fail("Create a new source to reopen an archive.", E_UNEXPECTED);
  _options = options;
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  if (!url || options.TimeoutMs <= 0 || options.RequestDeadlineMs == 0)
    return Fail("Invalid source options.", E_INVALIDARG);
  // Require an encoded URI, not a local path. Do not alter signed query strings.
  for (const wchar_t *p = url; *p; ++p)
    if (*p <= 0x20 || *p > 0x7e || *p == L'\\' || *p == L'#')
      return Fail("Use an ASCII percent-encoded HTTP(S) URL without a fragment.", E_INVALIDARG);
  URL_COMPONENTS parts = {};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url, 0, 0, &parts) || !parts.dwHostNameLength ||
      parts.dwUserNameLength || parts.dwPasswordLength ||
      (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
    return Fail("Expected an HTTP(S) URL without embedded credentials.", E_INVALIDARG);
  _secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  _path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (_path.empty()) _path = L"/";
  if (parts.dwExtraInfoLength) _path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  _session = WinHttpOpen(L"7-Zip-Range-Prototype/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!_session) return Fail("Cannot create HTTP session.", LastErrorResult());
  if (!WinHttpSetTimeouts(_session, options.TimeoutMs, options.TimeoutMs,
      options.TimeoutMs, options.TimeoutMs))
    return Fail("Cannot configure HTTP timeouts.", LastErrorResult());
  _connection = WinHttpConnect(_session, host.c_str(), parts.nPort, 0);
  if (!_connection) return Fail("Cannot create HTTP connection.", LastErrorResult());
  std::vector<unsigned char> probe;
  const HRESULT result = Request(0, 1, true, probe);
  _ready = result == S_OK;
  return result;
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
  data.clear();
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  const DWORD begun = GetTickCount();
  CRequestHandle request(WinHttpOpenRequest(_connection, L"GET", _path.c_str(), NULL,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      (_secure ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_ESCAPE_DISABLE));
  if (!request.Value) return Fail("Cannot create HTTP request.", LastErrorResult());
  // Bound the complete header phase too, not just individual socket reads.
  DWORD responseTimeout = (std::min)(static_cast<unsigned>(_options.TimeoutMs),
      _options.RequestDeadlineMs);
  if (!WinHttpSetTimeouts(request.Value, _options.TimeoutMs, _options.TimeoutMs,
      _options.TimeoutMs, _options.TimeoutMs) ||
      !WinHttpSetOption(request.Value, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
          &responseTimeout, sizeof(responseTimeout)))
    return Fail("Cannot configure request timeouts.", LastErrorResult());
  // This prototype never follows redirects, sends browser cookies, or negotiates credentials.
  DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
  if (!WinHttpSetOption(request.Value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
    return Fail("Cannot configure HTTP request policy.", LastErrorResult());
  const std::wstring headers = L"Accept-Encoding: identity\r\nRange: bytes=" +
      std::to_wstring(start) + L"-" + std::to_wstring(start + length - 1) + L"\r\n" +
      (probe ? L"" : L"If-Match: " + _etag + L"\r\n");
  ++Requests;
  if (!WinHttpSendRequest(request.Value, headers.c_str(), static_cast<DWORD>(headers.size()),
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.Value, NULL))
  {
    const HRESULT failure = LastErrorResult();
    return Fail(IsCancelled() ? "Cancelled." : "HTTP request failed or timed out.",
        IsCancelled() ? E_ABORT : failure);
  }
  if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
  DWORD status = 0, statusSize = sizeof(status);
  if (!WinHttpQueryHeaders(request.Value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    return Fail("Cannot read HTTP status.", LastErrorResult());
  Status = status;
  if (status == 200) return Fail("Server ignored Range; full download is not enabled.");
  if (status == 412) return Fail("Archive changed on the server (HTTP 412).");
  if (status >= 300 && status < 400) return Fail("Redirect unsupported; supply the final direct URL.");
  if (status != 206) return Fail("Expected HTTP 206; see status in statistics.");

  std::wstring range, tag, encoding, contentLength, contentType;
  ULONGLONG first = 0, last = 0, total = 0;
  if (Header(request.Value, WINHTTP_QUERY_CONTENT_RANGE, range) != S_OK ||
      !ParseRange(range, first, last, total) || first != start || last - first + 1 != length)
    return Fail("Invalid or mismatched Content-Range.");
  if (Header(request.Value, WINHTTP_QUERY_ETAG, tag) != S_OK || !StrongETag(tag))
    return Fail("A strong ETag is required for partial downloads.");
  if (!probe && (total != Size || tag != _etag))
    return Fail("Archive identity or size changed during the session.");
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
  std::vector<unsigned char> body(length);
  unsigned done = 0;
  for (;;)
  {
    if (IsCancelled()) return Fail("Cancelled.", E_ABORT);
    // Unsigned subtraction also handles the 32-bit tick counter wrapping.
    if (static_cast<DWORD>(GetTickCount() - begun) >= _options.RequestDeadlineMs)
      return Fail("HTTP request deadline exceeded.", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    unsigned char extra;
    DWORD got = 0;
    const DWORD want = done < length ? (std::min)(length - done, 65536u) : 1;
    if (!WinHttpReadData(request.Value, done < length ? body.data() + done : &extra, want, &got))
      return Fail("Range body was interrupted or timed out.", LastErrorResult());
    Downloaded += got;
    if (!got) break;
    if (done == length) return Fail("Range body exceeds its declared length.");
    done += got;
  }
  if (done != length) return Fail("Truncated range body.");
  if (probe) { Size = total; _etag = tag; }
  data.swap(body); // Publish only a complete, validated range.
  return S_OK;
}
}
