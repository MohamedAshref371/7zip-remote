#include "StdAfx.h"
#include "HttpInStream.h"

#include <limits>
#include <algorithm>
#include <utility>
#include <new>
#include <cstring>

HRESULT CHttpInStream::Open(const wchar_t *url, const NWindows::CHttpRangeOptions &options)
{
  try { _failure = Source.Open(url, options); }
  catch (const std::bad_alloc &) { _failure = E_OUTOFMEMORY; }
  catch (...) { _failure = E_FAIL; }
  return _failure;
}

Z7_COM7F_IMF(CHttpInStream::GetSize(UInt64 *size))
{
  if (!size) return E_POINTER;
  if (_failure != S_OK) return _failure;
  *size = Source.Size;
  return S_OK;
}

Z7_COM7F_IMF(CHttpInStream::Seek(Int64 offset, UInt32 origin, UInt64 *position))
{
  if (_failure != S_OK) return _failure;
  UInt64 base;
  switch (origin)
  {
    case STREAM_SEEK_SET: base = 0; break;
    case STREAM_SEEK_CUR: base = _pos; break;
    case STREAM_SEEK_END: base = Source.Size; break;
    default: return STG_E_INVALIDFUNCTION;
  }
  UInt64 next;
  if (offset < 0)
  {
    const UInt64 magnitude = static_cast<UInt64>(-(offset + 1)) + 1;
    if (magnitude > base) return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    next = base - magnitude;
  }
  else
  {
    const UInt64 limit = static_cast<UInt64>((std::numeric_limits<Int64>::max)());
    if (static_cast<UInt64>(offset) > limit - base) return STG_E_INVALIDFUNCTION;
    next = base + static_cast<UInt64>(offset);
  }
  _pos = next;
  if (position) *position = _pos;
  return S_OK;
}

Z7_COM7F_IMF(CHttpInStream::Read(void *data, UInt32 size, UInt32 *processed))
{
  if (processed) *processed = 0;
  if (!size) return S_OK;
  if (!data) return E_POINTER;
  if (_failure != S_OK) return _failure;
  if (Source.IsCancelled()) { _failure = E_ABORT; return _failure; }
  if (_pos >= Source.Size) return S_OK;
  try
  {
    const UInt64 start = _pos / kBlockSize * kBlockSize;
    auto block = _cache.begin();
    for (; block != _cache.end() && block->Start != start; ++block) {}
    if (block == _cache.end())
    {
      CBlock fetched;
      fetched.Start = start;
      const unsigned length = static_cast<unsigned>((std::min)(
          Source.Size - start, static_cast<UInt64>(kBlockSize)));
      _failure = Source.Fetch(start, length, fetched.Data);
      if (_failure != S_OK) { _cache.clear(); return _failure; }
      if (_cache.size() == kCacheBlocks) _cache.pop_back();
      _cache.push_front(std::move(fetched));
    }
    else
    {
      ++CacheHits;
      _cache.splice(_cache.begin(), _cache, block);
    }
    const CBlock &current = _cache.front();
    const size_t offset = static_cast<size_t>(_pos - start);
    const UInt32 count = static_cast<UInt32>((std::min)(
        static_cast<size_t>(size), current.Data.size() - offset));
    memcpy(data, current.Data.data() + offset, count);
    _pos += count;
    if (processed) *processed = count;
    return S_OK; // Short reads at block boundaries are intentional.
  }
  catch (const std::bad_alloc &) { _failure = E_OUTOFMEMORY; }
  catch (...) { _failure = E_FAIL; }
  _cache.clear();
  return _failure;
}
