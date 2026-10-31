// Optional metadata-only selection preview for HTTP archive folders.
#ifndef ZIP7_INC_REMOTE_SELECTION_H
#define ZIP7_INC_REMOTE_SELECTION_H

#include "../../IProgress.h"
#include "../../../Common/MyString.h"
#include "../../../Common/MyCom.h"

enum
{
  kRemoteSelection = 1,
  kRemoteSelectionUnpacked = 2,
  kRemoteSelectionDownload = 4,
  kRemoteSelectionSolid = 8
};

struct CRemoteSelectionEstimate
{
  UInt64 Unpacked;
  UInt64 Download;
  UInt32 Flags;
  CRemoteSelectionEstimate(): Unpacked(0), Download(0), Flags(0) {}
};

static const GUID IID_IRemoteSelection =
    { 0xb610d2c6, 0x9f18, 0x474b, { 0x91, 0x31, 0xb7, 0x2e, 0x83, 0x64, 0xe8, 0x05 } };
#define Z7_IFACEM_IRemoteSelection(x) \
  x(GetSelectionEstimate(const UInt32 *indices, UInt32 count, CRemoteSelectionEstimate *estimate))

Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IRemoteSelection: public IUnknown
{
  Z7_IFACE_COM7_PURE(IRemoteSelection)
};
Z7_PURE_INTERFACES_END

static const GUID IID_IRemoteCacheStatus =
    { 0x6ac90d38, 0x5de2, 0x4c92, { 0x8d, 0xc5, 0x0d, 0xc1, 0x18, 0x98, 0x14, 0x29 } };
#define Z7_IFACEM_IRemoteCacheStatus(x) \
  x(GetCacheStatus(BSTR *status, BSTR *entryId)) \
  x(SetCacheSuspended(UInt32 suspended)) \
  x(GetRemoteHttpStatus(UInt32 *status))
Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IRemoteCacheStatus: public IUnknown
{
  Z7_IFACE_COM7_PURE(IRemoteCacheStatus)
};
Z7_PURE_INTERFACES_END

// Scope a refresh while keeping the previous folder alive for rollback.
class CRemoteCacheRefreshScope
{
  CMyComPtr<IRemoteCacheStatus> _cache;
public:
  explicit CRemoteCacheRefreshScope(IUnknown *folder)
  {
    if (folder && folder->QueryInterface(IID_IRemoteCacheStatus, (void **)&_cache) == S_OK)
      _cache->SetCacheSuspended(1);
  }
  ~CRemoteCacheRefreshScope() { Restore(); }
  void Restore() { if (_cache) { _cache->SetCacheSuspended(0); _cache.Release(); } }
  void Commit() { _cache.Release(); }
private:
  CRemoteCacheRefreshScope(const CRemoteCacheRefreshScope &) = delete;
  CRemoteCacheRefreshScope &operator=(const CRemoteCacheRefreshScope &) = delete;
};

inline UString RemoteSelectionSize(UInt64 bytes)
{
  static const char * const units[] = {" B", " KiB", " MiB", " GiB", " TiB", " PiB", " EiB"};
  unsigned unit = 0;
  UInt64 divisor = 1;
  while (unit < 6 && bytes / divisor >= 1024) { divisor *= 1024; ++unit; }
  UString text;
  text.Add_UInt64(bytes / divisor);
  if (unit)
  {
    text += L'.';
    text.Add_UInt32((UInt32)((bytes % divisor) * 10 / divisor));
  }
  text += units[unit];
  return text;
}

inline void RemoteSelectionText(const CRemoteSelectionEstimate &estimate,
    UString &files, UString &download)
{
  files = "Unpacked: ";
  files += (estimate.Flags & kRemoteSelectionUnpacked) ? RemoteSelectionSize(estimate.Unpacked) : UString("unavailable");
  download = "Download: ";
  if (estimate.Flags & kRemoteSelectionDownload)
  {
    download += L'~';
    download += RemoteSelectionSize(estimate.Download);
  }
  else download += "unavailable";
  if (estimate.Flags & kRemoteSelectionSolid) download += " [Solid]";
}

#endif
