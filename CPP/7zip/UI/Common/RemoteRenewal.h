#ifndef ZIP7_INC_REMOTE_RENEWAL_H
#define ZIP7_INC_REMOTE_RENEWAL_H

#include "../../../Windows/HttpDiskCache.h"

inline bool IsRenewableHttpStatus(unsigned status)
{
  return status == 403 || status == 404 || status == 410;
}

// A changed URL never inherits consent to link a previous resource's cache.
inline NWindows::CHttpDiskCacheOptions RemoteRenewalCacheOptions(
    const NWindows::CHttpDiskCacheOptions &current,
    const std::wstring &entryId, bool consent)
{
  NWindows::CHttpDiskCacheOptions options = current;
  options.ReuseId.clear();
  options.AllowUnverified = false;
  if (options.Enabled && consent && !entryId.empty())
  {
    options.ReuseId = entryId;
    options.AllowUnverified = true;
  }
  return options;
}

#endif
