// Optional, conservative physical read ranges. Does not perform archive reads.
#ifndef ZIP7_INC_ARCHIVE_READ_RANGE_H
#define ZIP7_INC_ARCHIVE_READ_RANGE_H
#include "IArchive.h"

static const GUID IID_IArchiveReadRange =
    { 0x6cf45ac2, 0x9c32, 0x49a1, { 0xa7, 0xc0, 0xb4, 0x36, 0x59, 0x4f, 0x69, 0x12 } };
#define Z7_IFACEM_IArchiveReadRange(x) \
  x(GetItemReadRange(UInt32 index, UInt64 *start, UInt64 *length))
Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IArchiveReadRange: public IUnknown
{
  Z7_IFACE_COM7_PURE(IArchiveReadRange)
};
Z7_PURE_INTERFACES_END
#endif
