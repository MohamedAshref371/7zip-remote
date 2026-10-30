#include "../../CPP/Common/Common.h"
#include "../../CPP/Common/MyInitGuid.h"
#include "../../CPP/7zip/Common/HttpInStream.h"
#include <cstdio>

static DWORD g_Started;
static unsigned g_CancelMs;
static bool Cancel(void *) { return g_CancelMs && GetTickCount() - g_Started >= g_CancelMs; }
static std::string Utf8(const std::wstring &text)
{
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
  std::string result(size, 0);
  if (size) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &result[0], size, NULL, NULL);
  return result;
}
int wmain(int argc, wchar_t **argv)
{
  if (argc < 3) return 2;
  NWindows::CHttpRangeOptions options;
  options.DiskCache.Enabled = true;
  options.DiskCache.Directory = argv[2];
  unsigned blocks = 16, first = 0;
  bool hold = false;
  for (int i = 3; i < argc; ++i)
  {
    if (wcscmp(argv[i], L"--cache-prepare") == 0)
    {
      const bool ok = NWindows::CHttpDiskCache::Prepare(argv[2]);
      printf("PREPARED %u\n", ok ? 1u : 0u);
      return ok ? 0 : 1;
    }
    if (wcscmp(argv[i], L"--cache-clear") == 0)
    {
      unsigned skipped = 0;
      const bool ok = NWindows::CHttpDiskCache::Clear(argv[2], skipped);
      printf("CLEARED %u %u\n", ok ? 1u : 0u, skipped);
      return ok ? 0 : 1;
    }
    if (wcscmp(argv[i], L"--cache-list") == 0)
    {
      for (const auto &entry : NWindows::CHttpDiskCache::Catalog(argv[2]))
        printf("ENTRY %s %llu %llu %u\n", Utf8(entry.Id).c_str(), entry.Info.Size, entry.Used, (unsigned)entry.Info.Verified);
      return 0;
    }
    if (wcscmp(argv[i], L"--skip-http-etag") == 0) options.SkipETagVerification = true;
    if (wcscmp(argv[i], L"--cache-trust") == 0) options.DiskCache.AllowUnverified = true;
    if (wcscmp(argv[i], L"--cache-hold") == 0) hold = true;
    if (wcsncmp(argv[i], L"--cache-reuse=", 14) == 0) options.DiskCache.ReuseId = argv[i] + 14;
    if (wcsncmp(argv[i], L"--cache-blocks=", 15) == 0) blocks = (unsigned)_wtoi(argv[i] + 15);
    if (wcsncmp(argv[i], L"--cache-start=", 14) == 0) first = (unsigned)_wtoi(argv[i] + 14);
    if (wcsncmp(argv[i], L"--cache-limit=", 14) == 0) options.DiskCache.MaxBytes = _wcstoui64(argv[i] + 14, NULL, 10);
    if (wcsncmp(argv[i], L"--cache-cancel-ms=", 18) == 0) g_CancelMs = (unsigned)_wtoi(argv[i] + 18);
  }
  if (blocks > 64) return 2;
  options.CancelCheck = Cancel;
  g_Started = GetTickCount();
  CHttpInStream *spec = new CHttpInStream;
  CMyComPtr<IInStream> stream = spec;
  HRESULT result = spec->Open(argv[1], options);
  std::vector<unsigned char> data;
  if (result == S_OK)
  {
    result = stream->Seek((Int64)first * CHttpInStream::kBlockSize, STREAM_SEEK_SET, NULL);
    for (unsigned i = 0; i < blocks && result == S_OK; ++i)
    {
      std::vector<unsigned char> block(CHttpInStream::kBlockSize);
      UInt32 got = 0;
      result = stream->Read(block.data(), (UInt32)block.size(), &got);
      if (result == S_OK) data.insert(data.end(), block.begin(), block.begin() + got);
    }
  }
  BYTE digest[32];
  if (!NWindows::NHttpCache::Hash(data.data(), data.size(), digest)) return 2;
  printf("DISK %08lx %llu %llu %llu %s\n", (unsigned long)result,
      spec->Source.Requests, spec->Source.Downloaded, spec->DiskCacheHits, Utf8(spec->DiskCache.Id()).c_str());
  printf("HASH "); for (BYTE value : digest) printf("%02x", value); printf("\n");
  printf("STATUS %s\n", Utf8(spec->DiskCache.Status).c_str());
  if (hold) { printf("CACHE_READY\n"); fflush(stdout); if (getchar() != 'g') return 2; }
  return result == S_OK ? 0 : result == E_ABORT ? 3 : 1;
}
