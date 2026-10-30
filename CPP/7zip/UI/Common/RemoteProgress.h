// Optional progress channel, separate from archive/decompression progress.
#ifndef ZIP7_INC_REMOTE_PROGRESS_H
#define ZIP7_INC_REMOTE_PROGRESS_H

#include "../../IProgress.h"

// Extension-specific IID: do not consume an upstream 7-Zip interface number.
static const GUID IID_IRemoteProgress =
    { 0x6944361e, 0x2648, 0x4cf9, { 0xa2, 0x5e, 0x80, 0x6c, 0x8c, 0x69, 0x9d, 0xa1 } };

#define Z7_IFACEM_IRemoteProgress(x) \
  x(SetNetworkProgress(UInt64 downloaded, UInt64 archiveSize, UInt64 requests))

Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IRemoteProgress: public IUnknown
{
  Z7_IFACE_COM7_PURE(IRemoteProgress)
};
Z7_PURE_INTERFACES_END

#endif
