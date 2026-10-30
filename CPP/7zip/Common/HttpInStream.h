#ifndef ZIP7_INC_HTTP_IN_STREAM_H
#define ZIP7_INC_HTTP_IN_STREAM_H

#include <list>
#include "../../Common/MyCom.h"
#include "../../Windows/HttpRangeSource.h"
#include "../IStream.h"

struct CHttpReadRange
{
  UInt64 Start, Size;
};

class CHttpInStream Z7_final:
  public IInStream, public IStreamGetSize, public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_3(IInStream, ISequentialInStream, IStreamGetSize)
  Z7_IFACE_COM7_IMP(ISequentialInStream)
  Z7_IFACE_COM7_IMP(IInStream)
  Z7_IFACE_COM7_IMP(IStreamGetSize)
  struct CBlock
  {
    UInt64 Start;
    std::vector<unsigned char> Data;
  };
  std::list<CBlock> _cache;
  UInt64 _pos = 0;
  HRESULT _failure = E_UNEXPECTED;
public:
  // Single consumer. 32 x 256 KiB = 8 MiB maximum retained cache.
  static const unsigned kBlockSize = 256u << 10;
  static const unsigned kCacheBlocks = 32;
  NWindows::CHttpRangeSource Source;
  NWindows::CHttpDiskCache DiskCache;
  UInt64 CacheHits = 0;
  UInt64 DiskCacheHits = 0;
  HRESULT Open(const wchar_t *url, const NWindows::CHttpRangeOptions &options);
  bool EstimateMissingBytes(std::vector<CHttpReadRange> ranges, UInt64 &bytes) const;
};
#endif
