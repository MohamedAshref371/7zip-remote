#include "StdAfx.h"
#include "HttpInStream.h"

#include <limits>
#include <algorithm>
#include <utility>
#include <new>
#include <cstring>

bool CHttpInStream::EstimateMissingBytes(std::vector<CHttpReadRange> ranges, UInt64 &bytes) const
{
  bytes = 0;
  for (auto &range : ranges)
  {
    if (range.Start > Source.Size || range.Size > Source.Size - range.Start) return false;
    if (!range.Size) continue;
    if (_exactReads) continue;
    const UInt64 end = range.Start + range.Size;
    const UInt64 roundedEnd = (std::min)(Source.Size,
        ((end - 1) / _blockSize + 1) * _blockSize);
    range.Start = range.Start / _blockSize * _blockSize;
    range.Size = roundedEnd - range.Start;
  }
  std::sort(ranges.begin(), ranges.end(), [](const CHttpReadRange &a, const CHttpReadRange &b)
      { return a.Start < b.Start; });
  // Merge intervals rather than enumerating blocks, even for huge archives.
  std::vector<CHttpReadRange> merged;
  for (const auto &range : ranges)
  {
    if (!range.Size) continue;
    if (!merged.empty() && range.Start <= merged.back().Start + merged.back().Size)
    {
      const UInt64 end = (std::max)(merged.back().Start + merged.back().Size, range.Start + range.Size);
      merged.back().Size = end - merged.back().Start;
    }
    else merged.push_back(range);
  }
  if (_exactReads)
  {
    for (const auto &range : merged)
    {
      bytes += range.Size;
      std::vector<CHttpReadRange> cached;
      for (const auto &part : DiskCache.Blocks()) cached.push_back({part.first, part.second});
      for (const auto &block : _cache)
        cached.push_back({block.Start, block.Data.size()});
      std::sort(cached.begin(), cached.end(), [](const CHttpReadRange &a, const CHttpReadRange &b)
          { return a.Start < b.Start; });
      UInt64 coveredEnd = range.Start;
      for (const auto &block : cached)
      {
        const UInt64 start = (std::max)(coveredEnd, block.Start);
        const UInt64 end = (std::min)(range.Start + range.Size, block.Start + block.Size);
        if (end > start) bytes -= end - start;
        coveredEnd = (std::max)(coveredEnd, end);
      }
    }
    return true;
  }
  for (const auto &range : merged)
  {
    bytes += range.Size;
    const auto &disk = DiskCache.Blocks();
    for (auto block = disk.lower_bound(range.Start); block != disk.end() && block->first < range.Start + range.Size; ++block)
      bytes -= block->second;
    for (const auto &block : _cache)
      if (block.Start >= range.Start && block.Start < range.Start + range.Size && !disk.count(block.Start))
        bytes -= block.Data.size();
  }
  return true;
}

HRESULT CHttpInStream::Open(const wchar_t *url, const NWindows::CHttpRangeOptions &options)
{
  if (options.BlockSize != (32u << 10) && options.BlockSize != (64u << 10)
      && options.BlockSize != (128u << 10)
      && options.BlockSize != kBlockSize) return E_INVALIDARG;
  _exactReads = options.ExactReads;
  _blockSize = _exactReads ? kBlockSize : options.BlockSize;
  try
  {
    _failure = Source.Open(url, options);
    if (_failure == S_OK)
    {
      if (options.DiskCache.Enabled) Source.ReportStage(NWindows::kHttpCheckingCache);
      try { _failure = DiskCache.Open(options.DiskCache, url, Source, _exactReads, _blockSize); }
      catch (...) { DiskCache.Close(); DiskCache.Status = L"Cache unavailable (memory only)"; }
    }
    if (_failure == S_OK) Source.ReportStage(NWindows::kHttpReadingHeaders);
  }
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
    const UInt64 start = _exactReads ? _pos : _pos / _blockSize * _blockSize;
    auto block = _cache.begin();
    for (; block != _cache.end(); ++block)
      if (_exactReads ? (_pos >= block->Start && _pos - block->Start < block->Data.size())
          : block->Start == start) break;
    if (block == _cache.end())
    {
      CBlock fetched;
      fetched.Start = start;
      unsigned length = static_cast<unsigned>((std::min)(
          Source.Size - start, static_cast<UInt64>(_blockSize)));
      if (_exactReads)
      {
        length = (std::min)(length, size);
        // Stop before an existing entry: never refetch bytes still in memory.
        for (const auto &cached : _cache)
          if (cached.Start > start && cached.Start - start < length)
            length = static_cast<unsigned>(cached.Start - start);
      }
      bool diskHit = false;
      try { diskHit = _exactReads ? DiskCache.ReadRange(start, length, fetched.Data)
          : DiskCache.Read(start, length, fetched.Data); }
      catch (...) { DiskCache.Close(); DiskCache.Status = L"Cache read failed (memory only)"; }
      if (diskHit) ++DiskCacheHits;
      else
      {
        if (_exactReads) length = DiskCache.MissingPrefix(start, length);
        _failure = Source.Fetch(start, length, fetched.Data);
        if (_failure == S_OK && !DiskCache.MatchesEndpoint(Source.FinalUrl()))
        {
          const bool oldBytesRead = DiskCacheHits != 0 || !_cache.empty();
          DiskCache.Quarantine();
          DiskCache.Close();
          DiskCache.Status = L"Cache source location changed; reopen the archive";
          if (oldBytesRead)
          {
            Source.Error = "Archive location changed while cached data was in use. Reopen the archive or use a fresh final direct URL.";
            _failure = E_FAIL; _cache.clear(); return _failure;
          }
        }
        if (_failure == S_OK)
          try { DiskCache.Write(start, fetched.Data); }
          catch (...) { DiskCache.Status = L"Cache write failed; new data kept in memory"; }
      }
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
    const size_t offset = static_cast<size_t>(_pos - current.Start);
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
