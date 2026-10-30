// Experimental Windows HTTP byte-range transport. No archive dependencies.
#ifndef ZIP7_INC_HTTP_RANGE_SOURCE_H
#define ZIP7_INC_HTTP_RANGE_SOURCE_H

#include <string>
#include <vector>
#include "../Common/MyWindows.h"

namespace NWindows {

struct CHttpRangeOptions
{
  HANDLE CancelEvent = NULL; // caller-owned, must outlive the source
  bool (*CancelCheck)(void *) = NULL;
  void *CancelContext = NULL; // caller-owned, must outlive the source
  int TimeoutMs = 3000;
  unsigned RequestDeadlineMs = 30000;
  bool SkipETagVerification = false; // explicit per-session opt-in; range/size checks remain mandatory
};

class CHttpRangeSource
{
  void *_session = NULL;
  void *_connection = NULL;
  std::wstring _path;
  std::wstring _etag;
  bool _secure = false;
  bool _ready = false;
  CHttpRangeOptions _options;
  HRESULT Request(ULONGLONG start, unsigned length, bool probe,
      std::vector<unsigned char> &data);
  HRESULT Fail(const char *message, HRESULT result = E_FAIL);
  HRESULT FailNetwork();
  CHttpRangeSource(const CHttpRangeSource &) = delete;
  CHttpRangeSource &operator=(const CHttpRangeSource &) = delete;
public:
  CHttpRangeSource() = default;
  ~CHttpRangeSource();
  ULONGLONG Size = 0;
  ULONGLONG Requests = 0;
  ULONGLONG Downloaded = 0; // body bytes actually read, including rejected bodies
  unsigned Status = 0;
  std::string Error;
  HRESULT Open(const wchar_t *url, const CHttpRangeOptions &options);
  HRESULT Fetch(ULONGLONG start, unsigned length, std::vector<unsigned char> &data);
  bool IsCancelled() const;
  void SetCancelCheck(bool (*check)(void *), void *context)
  {
    _options.CancelCheck = check;
    _options.CancelContext = context;
  }
};

}
#endif
