#ifndef ZIP7_INC_TRACING_IN_STREAM_H
#define ZIP7_INC_TRACING_IN_STREAM_H

#include <cstdio>
#include "../../Common/MyCom.h"
#include "../IStream.h"

class CTracingInStream Z7_final:
  public IInStream, public IStreamGetSize, public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_3(IInStream, ISequentialInStream, IStreamGetSize)
  Z7_IFACE_COM7_IMP(ISequentialInStream)
  Z7_IFACE_COM7_IMP(IInStream)
  Z7_IFACE_COM7_IMP(IStreamGetSize)
  CMyComPtr<IInStream> _source;
  FILE *_log; // caller-owned; must outlive this wrapper
  UInt64 _position = 0;
public:
  UInt64 ReadCalls = 0;
  UInt64 ReadBytes = 0;
  const char *Phase = "open";
  CTracingInStream(IInStream *source, FILE *log): _source(source), _log(log)
  {
    if (_log) fputs("phase,operation,offset,requested,processed,origin,result\n", _log);
  }
};
#endif
