#include "StdAfx.h"
#include "TracingInStream.h"

Z7_COM7F_IMF(CTracingInStream::Read(void *data, UInt32 size, UInt32 *processed))
{
  UInt32 count = 0;
  const HRESULT result = _source->Read(data, size, &count);
  ++ReadCalls;
  ReadBytes += count;
  if (_log) fprintf(_log, "%s,read,%llu,%u,%u,0,%08lx\n", Phase,
      static_cast<unsigned long long>(_position), size, count, static_cast<unsigned long>(result));
  _position += count;
  if (processed) *processed = count;
  return result;
}

Z7_COM7F_IMF(CTracingInStream::Seek(Int64 offset, UInt32 origin, UInt64 *position))
{
  UInt64 next = 0;
  const HRESULT result = _source->Seek(offset, origin, &next);
  if (result == S_OK) _position = next;
  if (_log) fprintf(_log, "%s,seek,%llu,%lld,0,%u,%08lx\n", Phase,
      static_cast<unsigned long long>(_position), static_cast<long long>(offset), origin,
      static_cast<unsigned long>(result));
  if (position && result == S_OK) *position = next;
  return result;
}

Z7_COM7F_IMF(CTracingInStream::GetSize(UInt64 *size))
{
  if (!size) return E_POINTER;
  CMyComPtr<IStreamGetSize> getSize;
  if (_source.QueryInterface(IID_IStreamGetSize, &getSize) == S_OK && getSize)
    return getSize->GetSize(size);
  UInt64 saved = 0;
  HRESULT result = _source->Seek(0, STREAM_SEEK_CUR, &saved);
  if (result != S_OK) return result;
  result = _source->Seek(0, STREAM_SEEK_END, size);
  const HRESULT restore = _source->Seek(static_cast<Int64>(saved), STREAM_SEEK_SET, NULL);
  return result != S_OK ? result : restore;
}
