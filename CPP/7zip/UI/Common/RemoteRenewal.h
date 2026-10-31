#ifndef ZIP7_INC_REMOTE_RENEWAL_H
#define ZIP7_INC_REMOTE_RENEWAL_H

#include "../../../Windows/HttpDiskCache.h"
#include "../FileManager/IFolder.h"
#include "../../../Common/MyCom.h"

// BindToFolder accepts one name, not an archive-relative path or trailing slash.
inline HRESULT BindRemoteArchivePath(IFolderFolder *root, const UString &path,
    CMyComPtr<IFolderFolder> &result)
{
  result.Release();
  CMyComPtr<IFolderFolder> current = root;
  if (!current) return E_INVALIDARG;
  unsigned start = 0;
  while (start < path.Len())
  {
    unsigned end = start;
    while (end < path.Len() && path[end] != L'/' && path[end] != L'\\') ++end;
    if (end > start)
    {
      UString name; name.SetFrom(path.Ptr(start), end - start);
      CMyComPtr<IFolderFolder> next;
      const HRESULT hr = current->BindToFolder(name, &next);
      if (hr != S_OK || !next) return hr == S_OK ? E_FAIL : hr;
      current = next;
    }
    start = end + 1;
  }
  result = current;
  return S_OK;
}

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
