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

// Multi-range extension; the handler calls the sink synchronously and never
// retains it. Separate IIDs preserve the original single-range interface.
static const GUID IID_IArchiveReadRangeSink =
    { 0x7b644153, 0xc2a8, 0x4a1f, { 0xbc, 0xb2, 0x97, 0x81, 0x0d, 0x45, 0x6b, 0xa8 } };
static const GUID IID_IArchiveReadRanges =
    { 0xd61d0c73, 0x3b16, 0x4895, { 0x93, 0xd1, 0x5e, 0x07, 0x19, 0xbd, 0xc5, 0x11 } };
#define Z7_IFACEM_IArchiveReadRangeSink(x) \
  x(AddReadRange(UInt64 start, UInt64 length))
#define Z7_IFACEM_IArchiveReadRanges(x) \
  x(GetItemReadRanges(UInt32 index, IArchiveReadRangeSink *sink))
Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IArchiveReadRangeSink: public IUnknown
{
  Z7_IFACE_COM7_PURE(IArchiveReadRangeSink)
};
struct Z7_DECLSPEC_NOVTABLE IArchiveReadRanges: public IUnknown
{
  Z7_IFACE_COM7_PURE(IArchiveReadRanges)
};
Z7_PURE_INTERFACES_END
#endif
