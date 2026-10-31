// Optional opening progress. Separate IID preserves upstream callback ABI.
#ifndef ZIP7_INC_ARCHIVE_OPEN_FILE_PROGRESS_H
#define ZIP7_INC_ARCHIVE_OPEN_FILE_PROGRESS_H
#include "IArchive.h"

static const GUID IID_IArchiveOpenFileProgress =
    { 0x5217b3a8, 0xe78f, 0x46e9, { 0xa0, 0x89, 0x64, 0x0b, 0xb1, 0x99, 0xe5, 0x30 } };
#define Z7_IFACEM_IArchiveOpenFileProgress(x) \
  x(SetCurrentFileName(const wchar_t *name, UInt64 files))
Z7_PURE_INTERFACES_BEGIN
struct Z7_DECLSPEC_NOVTABLE IArchiveOpenFileProgress: public IUnknown
{
  Z7_IFACE_COM7_PURE(IArchiveOpenFileProgress)
};
Z7_PURE_INTERFACES_END
#endif
