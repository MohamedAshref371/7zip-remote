// Private, versioned HTTP block cache. Cache I/O failures never fail extraction.
#ifndef ZIP7_INC_HTTP_DISK_CACHE_H
#define ZIP7_INC_HTTP_DISK_CACHE_H
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <cstddef>

namespace NWindows {
struct CHttpDiskCacheOptions
{
  bool Enabled = false;
  std::wstring Directory;
  ULONGLONG MaxBytes = 1ull << 30; // zero means unlimited
  std::wstring ReuseId; // session-only; never a filesystem path
  bool AllowUnverified = false; // separate, explicit consent for each open
};

namespace NHttpCache {
struct CCacheHandle
{
  HANDLE Value;
  explicit CCacheHandle(HANDLE value = INVALID_HANDLE_VALUE): Value(value) {}
  ~CCacheHandle() { if (Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
  bool Valid() const { return Value != INVALID_HANDLE_VALUE; }
private:
  CCacheHandle(const CCacheHandle &) = delete;
  CCacheHandle &operator=(const CCacheHandle &) = delete;
};
inline bool Hash(const void *data, size_t size, BYTE *digest)
{
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
  bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
  if (ok) ok = size <= MAXDWORD && CryptHashData(hash, (const BYTE *)data, (DWORD)size, 0) != FALSE;
  DWORD length = 32;
  if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) != FALSE && length == 32;
  if (hash) CryptDestroyHash(hash);
  CryptReleaseContext(provider, 0);
  return ok;
}
inline bool HashText(const std::wstring &text, BYTE *digest)
{ return Hash(text.data(), text.size() * sizeof(wchar_t), digest); }
inline std::wstring Hex(ULONGLONG value)
{
  std::wstring text(16, L'0');
  for (unsigned i = 0; i < 16; ++i) { text[15 - i] = L"0123456789abcdef"[value & 15]; value >>= 4; }
  return text;
}
inline bool IsHex(const std::wstring &s, size_t length)
{
  if (s.size() != length) return false;
  for (wchar_t c : s) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
  return true;
}
inline std::wstring RandomId()
{
  HCRYPTPROV provider = 0;
  ULONGLONG values[2];
  if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return L"";
  const bool ok = CryptGenRandom(provider, sizeof(values), (BYTE *)values) != FALSE;
  CryptReleaseContext(provider, 0);
  return ok ? Hex(values[0]) + Hex(values[1]) : L"";
}
inline bool Regular(const std::wstring &path, bool directory)
{
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)
      && bool(attrs & FILE_ATTRIBUTE_DIRECTORY) == directory;
}
inline std::wstring Root(const std::wstring &directory, bool create)
{
  if (directory.empty() || directory.compare(0, 4, L"\\\\.\\") == 0) return L"";
  std::vector<wchar_t> buffer(32768);
  const DWORD count = GetFullPathNameW(directory.c_str(), (DWORD)buffer.size(), buffer.data(), NULL);
  if (!count || count >= buffer.size() - 100) return L"";
  std::wstring path(buffer.data());
  if (path.compare(0, 4, L"\\\\?\\") != 0)
    path = path.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
  while (path.size() > 7 && path.back() == L'\\') path.pop_back();
  if (path.back() != L'\\') path += L'\\';
  path += L".7z-http-cache-v1";
  size_t start = 7;
  if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
  {
    start = path.find(L'\\', 8);
    if (start == std::wstring::npos) return L"";
    start = path.find(L'\\', start + 1);
    if (start == std::wstring::npos) return L"";
  }
  for (size_t i = start; i <= path.size(); ++i)
    if (i == path.size() || path[i] == L'\\')
    {
      const std::wstring part = path.substr(0, i);
      if (!Regular(part, true) && (!create || !CreateDirectoryW(part.c_str(), NULL))) return L"";
      if (!Regular(part, true)) return L"";
    }
  return path;
}
inline bool ReadFileBytes(const std::wstring &path, std::vector<BYTE> &bytes, DWORD maximum)
{
  if (!Regular(path, false)) return false;
  CCacheHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL));
  LARGE_INTEGER size;
  if (!file.Valid() || !GetFileSizeEx(file.Value, &size) || size.QuadPart < 0 || (ULONGLONG)size.QuadPart > maximum) return false;
  bytes.resize((size_t)size.QuadPart);
  DWORD got = 0;
  return ::ReadFile(file.Value, bytes.data(), (DWORD)bytes.size(), &got, NULL) && got == bytes.size();
}
inline bool AtomicWrite(const std::wstring &path, const void *data, DWORD size)
{
  const std::wstring id = RandomId();
  if (id.empty()) return false;
  const std::wstring temporary = path + L".tmp-" + id;
  bool ok = false;
  {
    CCacheHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL));
    DWORD wrote = 0;
    if (file.Valid()) ok = ::WriteFile(file.Value, data, size, &wrote, NULL) && wrote == size && FlushFileBuffers(file.Value);
  }
  if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!ok) DeleteFileW(temporary.c_str());
  return ok;
}
inline void Touch(const std::wstring &path)
{
  CCacheHandle file(CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, NULL));
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  if (file.Valid()) SetFileTime(file.Value, NULL, NULL, &now);
}
struct CInfo
{
  BYTE Magic[8];
  ULONGLONG Size;
  BYTE Url[32], Endpoint[32], Tag[32], Fingerprint[32];
  DWORD Verified;
  wchar_t Name[161];
  BYTE Digest[32];
};
struct CEntry
{
  std::wstring Id;
  CInfo Info;
  ULONGLONG Used = 0, LastUse = 0;
  bool Valid = true;
};
struct CBlockHeader
{
  BYTE Magic[8];
  ULONGLONG Offset;
  DWORD Size;
  BYTE Digest[32];
  BYTE Archive[32];
};
inline bool BlockName(const std::wstring &name, ULONGLONG &offset)
{
  if (name.size() != 21 || name[0] != L'b' || name.substr(17) != L".bin" || !IsHex(name.substr(1, 16), 16)) return false;
  offset = 0;
  for (unsigned i = 1; i < 17; ++i) offset = (offset << 4) | (name[i] <= L'9' ? name[i] - L'0' : name[i] - L'a' + 10);
  return true;
}
inline bool OwnedFile(const std::wstring &name)
{
  ULONGLONG offset;
  if (name == L"info.bin" || name == L"entry.owner" || name == L"session.lock" || BlockName(name, offset)) return true;
  const size_t pos = name.find(L".tmp-");
  return pos != std::wstring::npos && IsHex(name.substr(pos + 5), 32)
      && (name.substr(0, pos) == L"info.bin" || name.substr(0, pos) == L"entry.owner" || BlockName(name.substr(0, pos), offset));
}
inline bool Info(const std::wstring &folder, CInfo &info)
{
  std::vector<BYTE> bytes;
  if (!ReadFileBytes(folder + L"\\info.bin", bytes, sizeof(info)) || bytes.size() != sizeof(info)) return false;
  memcpy(&info, bytes.data(), sizeof(info));
  BYTE digest[32];
  return memcmp(info.Magic, "7ZHC01I", 8) == 0 && info.Name[160] == 0 && info.Verified <= 1
      && info.Size > 0 && info.Size <= 0x7fffffffffffffffull
      && Hash(&info, offsetof(CInfo, Digest), digest) && !memcmp(digest, info.Digest, 32);
}
inline bool SaveInfo(const std::wstring &folder, CInfo &info)
{
  return Hash(&info, offsetof(CInfo, Digest), info.Digest)
      && AtomicWrite(folder + L"\\info.bin", &info, sizeof(info));
}
inline std::vector<WIN32_FIND_DATAW> Children(const std::wstring &folder)
{
  std::vector<WIN32_FIND_DATAW> files;
  WIN32_FIND_DATAW data;
  const HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
  if (find != INVALID_HANDLE_VALUE)
  {
    do { if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) files.push_back(data); }
    while (FindNextFileW(find, &data));
    FindClose(find);
  }
  return files;
}
inline bool Marker(const std::wstring &root, bool create = true)
{
  const std::wstring path = root + L"\\owner.bin";
  static const char magic[] = "7-Zip private HTTP block cache v1\r\n";
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
  {
    if (!create) return false;
    for (const auto &child : Children(root))
      if (wcscmp(child.cFileName, L".") && wcscmp(child.cFileName, L"..")) return false;
    CCacheHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    DWORD wrote;
    if (file.Valid() && (!::WriteFile(file.Value, magic, sizeof(magic), &wrote, NULL) || wrote != sizeof(magic))) return false;
  }
  std::vector<BYTE> bytes;
  return ReadFileBytes(path, bytes, sizeof(magic)) && bytes.size() == sizeof(magic) && !memcmp(bytes.data(), magic, sizeof(magic));
}
inline std::vector<CEntry> CatalogRoot(const std::wstring &root)
{
  std::vector<CEntry> entries;
  for (const auto &file : Children(root))
  {
    CEntry entry;
    entry.Id = file.cFileName;
    if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsHex(entry.Id, 32)) continue;
    const std::wstring folder = root + L"\\" + entry.Id;
    if (!Info(folder, entry.Info))
    {
      std::vector<BYTE> owner;
      if (!ReadFileBytes(folder + L"\\entry.owner", owner, 16) || owner.size() != 8 || memcmp(owner.data(), "7ZHC01E", 8)) continue;
      memset(&entry.Info, 0, sizeof(entry.Info));
      memcpy(entry.Info.Name, L"Damaged cache entry", sizeof(L"Damaged cache entry"));
      entry.Valid = false;
    }
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExW((folder + L"\\info.bin").c_str(), GetFileExInfoStandard, &attrs))
      entry.LastUse = ((ULONGLONG)attrs.ftLastWriteTime.dwHighDateTime << 32) | attrs.ftLastWriteTime.dwLowDateTime;
    for (const auto &block : Children(folder))
      if (!(block.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && OwnedFile(block.cFileName))
        entry.Used += ((ULONGLONG)block.nFileSizeHigh << 32) | block.nFileSizeLow;
    entries.push_back(entry);
  }
  return entries;
}
}

class CHttpDiskCache
{
  std::wstring _root, _folder, _id, _liveEndpoint;
  HANDLE _lock = INVALID_HANDLE_VALUE;
  ULONGLONG _maximum = 0;
  NHttpCache::CInfo _info = {};
  std::map<ULONGLONG, unsigned> _blocks;
  bool BlockIdentity(BYTE *digest) const
  {
    // Bind each part to this cache generation and source identity as well as its
    // offset/content digest. A valid part copied from another entry is rejected.
    static_assert(offsetof(NHttpCache::CInfo, Verified) - offsetof(NHttpCache::CInfo, Size) == 136,
        "Cache identity layout changed");
    BYTE identity[168];
    memcpy(identity, &_info.Size, 136);
    return NHttpCache::HashText(_id, identity + 136) && NHttpCache::Hash(identity, sizeof(identity), digest);
  }
  std::wstring BlockPath(ULONGLONG offset) const { return _folder + L"\\b" + NHttpCache::Hex(offset) + L".bin"; }
  bool LockEntry(const std::wstring &id)
  {
    if (!NHttpCache::IsHex(id, 32)) return false;
    _folder = _root + L"\\" + id;
    if (!NHttpCache::Regular(_folder, true)) return false;
    _lock = CreateFileW((_folder + L"\\session.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (_lock == INVALID_HANDLE_VALUE) { _folder.clear(); return false; }
    _id = id;
    for (const auto &file : NHttpCache::Children(_folder))
    {
      const std::wstring name(file.cFileName);
      if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name.find(L".tmp-") != std::wstring::npos
          && NHttpCache::OwnedFile(name)) DeleteFileW((_folder + L"\\" + name).c_str());
    }
    return true;
  }
public:
  std::wstring Status;
  bool Enabled() const { return _lock != INVALID_HANDLE_VALUE; }
  const std::map<ULONGLONG, unsigned> &Blocks() const { return _blocks; }
  const std::wstring &Id() const { return _id; }
  bool MatchesEndpoint(const std::wstring &url) const
  { return !Enabled() || !_info.Verified || _liveEndpoint == url; }
  void Quarantine()
  {
    if (Enabled()) { _info.Verified = 0; NHttpCache::SaveInfo(_folder, _info); }
  }
  ~CHttpDiskCache() { Close(); }
  void Close()
  {
    if (_lock != INVALID_HANDLE_VALUE) CloseHandle(_lock);
    _lock = INVALID_HANDLE_VALUE;
    _folder.clear(); _id.clear(); _blocks.clear(); _liveEndpoint.clear();
  }
  static std::vector<NHttpCache::CEntry> Catalog(const std::wstring &directory)
  {
    const std::wstring root = NHttpCache::Root(directory, false);
    return root.empty() || !NHttpCache::Marker(root, false) ? std::vector<NHttpCache::CEntry>() : NHttpCache::CatalogRoot(root);
  }
  static bool Prepare(const std::wstring &directory)
  {
    const std::wstring root = NHttpCache::Root(directory, true);
    if (root.empty() || !NHttpCache::Marker(root)) return false;
    const std::wstring path = root + L"\\probe-" + NHttpCache::RandomId();
    NHttpCache::CCacheHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL));
    return file.Valid();
  }
  // Returns skipped active entries. Unexpected files/directories are never removed.
  static bool Clear(const std::wstring &directory, unsigned &skipped)
  {
    skipped = 0;
    const std::wstring root = NHttpCache::Root(directory, false);
    if (root.empty())
    {
      const DWORD attrs = GetFileAttributesW((directory + L"\\.7z-http-cache-v1").c_str());
      const DWORD error = GetLastError();
      return !directory.empty() && attrs == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
    }
    if (!NHttpCache::Marker(root, false)) return false;
    NHttpCache::CCacheHandle manager(CreateFileW((root + L"\\manage.lock").c_str(), GENERIC_WRITE, 0,
        NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
    if (!manager.Valid()) return false;
    bool ok = true;
    for (const auto &entry : NHttpCache::CatalogRoot(root))
    {
      const std::wstring folder = root + L"\\" + entry.Id;
      {
        NHttpCache::CCacheHandle lock(CreateFileW((folder + L"\\session.lock").c_str(), GENERIC_WRITE, 0,
            NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
        if (!lock.Valid()) { ++skipped; continue; }
        bool entryOk = true;
        for (const auto &file : NHttpCache::Children(folder))
          if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && NHttpCache::OwnedFile(file.cFileName)
              && wcscmp(file.cFileName, L"session.lock") != 0 && wcscmp(file.cFileName, L"info.bin") != 0
              && wcscmp(file.cFileName, L"entry.owner") != 0)
            if (!DeleteFileW((folder + L"\\" + file.cFileName).c_str())) entryOk = false;
        if (entryOk)
        { DeleteFileW((folder + L"\\info.bin").c_str()); DeleteFileW((folder + L"\\entry.owner").c_str()); }
        else ok = false;
      }
      DeleteFileW((folder + L"\\session.lock").c_str());
      RemoveDirectoryW(folder.c_str());
    }
    return ok;
  }
  template<class TSource>
  HRESULT Open(const CHttpDiskCacheOptions &options, const std::wstring &url, TSource &source)
  {
    using namespace NHttpCache;
    if (!options.Enabled) return S_OK;
    Status = L"Cache unavailable (memory only)";
    _root = Root(options.Directory, true);
    _maximum = options.MaxBytes;
    if (_root.empty() || !Marker(_root)) return S_OK;
    CCacheHandle manager(CreateFileW((_root + L"\\manage.lock").c_str(), GENERIC_WRITE, 0,
        NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
    if (!manager.Valid()) { Status = L"Cache busy (memory only)"; return S_OK; }
    BYTE urlHash[32], endpoint[32], tag[32], emptyTag[32];
    _liveEndpoint = source.FinalUrl();
    const std::wstring expectedEndpoint = _liveEndpoint;
    if (!HashText(url, urlHash) || !HashText(_liveEndpoint, endpoint) || !HashText(source.ETag(), tag)
        || !HashText(L"", emptyTag)) return S_OK;
    CEntry candidate;
    bool found = false, rejected = false;
    for (const auto &entry : CatalogRoot(_root))
    {
      if (!entry.Valid) continue;
      const bool sameUrl = !memcmp(entry.Info.Url, urlHash, 32) && !memcmp(entry.Info.Endpoint, endpoint, 32);
      if ((!options.ReuseId.empty() && options.ReuseId == entry.Id) || (options.ReuseId.empty() && sameUrl))
      {
        if (entry.Info.Size != source.Size || (sameUrl && memcmp(entry.Info.Tag, emptyTag, 32) && !source.ETag().empty()
            && memcmp(entry.Info.Tag, tag, 32))) { rejected = true; continue; }
        if ((!options.AllowUnverified && (!sameUrl || !entry.Info.Verified || source.SkipsETag()
            || memcmp(entry.Info.Tag, tag, 32)))) continue;
        if (!found || entry.LastUse > candidate.LastUse) { candidate = entry; found = true; }
      }
    }
    // The fresh strong-ETag probe validates an exact resource/version match.
    // Other cases need fresh edge metadata and, on reuse, stored-block samples.
    BYTE edgeHash[32];
    const bool strictCandidate = found && candidate.Info.Verified && !source.SkipsETag()
        && !memcmp(candidate.Info.Url, urlHash, 32) && !memcmp(candidate.Info.Endpoint, endpoint, 32)
        && !memcmp(candidate.Info.Tag, tag, 32);
    HRESULT result = S_OK;
    if (strictCandidate) memcpy(edgeHash, candidate.Info.Fingerprint, 32);
    else
    {
      std::vector<unsigned char> first, last, fingerprint;
      const unsigned length = (unsigned)((std::min)(source.Size, (ULONGLONG)65536));
      result = source.Fetch(0, length, first);
      if (result != S_OK) return result;
      if (source.Size <= length) last = first;
      else
      {
        result = source.Fetch(source.Size - length, length, last);
        if (result != S_OK) return result;
      }
      fingerprint = first;
      fingerprint.insert(fingerprint.end(), last.begin(), last.end());
      if (!Hash(fingerprint.data(), fingerprint.size(), edgeHash)) return S_OK;
    }
    if (expectedEndpoint != source.FinalUrl())
    { Status = L"Cache paused after a source redirect (memory only)"; return S_OK; }
    if (found && memcmp(candidate.Info.Fingerprint, edgeHash, 32)) { found = false; rejected = true; }
    if (found)
    {
      if (!LockEntry(candidate.Id)) { Status = L"Cache busy (memory only)"; return S_OK; }
      _info = candidate.Info;
      const bool verified = _info.Verified && !source.SkipsETag()
          && !memcmp(_info.Url, urlHash, 32) && !memcmp(_info.Endpoint, endpoint, 32)
          && !memcmp(_info.Tag, tag, 32);
      Status = verified ? L"Cache verified" : L"Cache reused by consent (partial verification)";
      for (const auto &file : Children(_folder))
      {
        if (source.IsCancelled()) { Close(); return E_ABORT; }
        ULONGLONG offset = 0;
        if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !BlockName(file.cFileName, offset)
            || offset % 262144 || offset >= source.Size) continue;
        const unsigned count = (unsigned)((std::min)(source.Size - offset, (ULONGLONG)262144));
        if (!file.nFileSizeHigh && file.nFileSizeLow == sizeof(CBlockHeader) + count) _blocks[offset] = count;
      }
      // Sample up to three stored blocks, always bypassing the disk/memory caches.
      if (!verified && !_blocks.empty())
      {
        std::vector<ULONGLONG> offsets;
        for (const auto &block : _blocks) offsets.push_back(block.first);
        std::vector<size_t> samples = {size_t(0), offsets.size() / 2, offsets.size() - 1};
        samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
        for (size_t index : samples)
        {
          std::vector<unsigned char> stored, fresh;
          const ULONGLONG offset = offsets[index];
          if (!Read(offset, _blocks[offset], stored)) continue;
          result = source.Fetch(offset, (unsigned)stored.size(), fresh);
          if (result != S_OK) { Close(); return result; }
          if (fresh != stored) { Close(); rejected = true; found = false; break; }
        }
      }
      if (expectedEndpoint != source.FinalUrl())
      { Close(); Status = L"Cache paused after a source redirect (memory only)"; return S_OK; }
      if (found)
      {
        if (!verified)
        {
          _info.Verified = 0; // Never upgrade mixed/unverified cached bytes to verified.
          if (!SaveInfo(_folder, _info)) { Close(); Status = L"Cache update failed (memory only)"; return S_OK; }
        }
        Touch(_folder + L"\\info.bin"); Prune(); return S_OK;
      }
    }
    _liveEndpoint = expectedEndpoint;
    const std::wstring id = RandomId();
    if (id.empty() || !CreateDirectoryW((_root + L"\\" + id).c_str(), NULL) || !LockEntry(id)) return S_OK;
    if (!AtomicWrite(_folder + L"\\entry.owner", "7ZHC01E", 8)) { Close(); return S_OK; }
    memset(&_info, 0, sizeof(_info));
    memcpy(_info.Magic, "7ZHC01I", 8);
    _info.Size = source.Size;
    _info.Verified = source.SkipsETag() ? 0 : 1;
    memcpy(_info.Url, urlHash, 32); memcpy(_info.Endpoint, endpoint, 32);
    memcpy(_info.Tag, tag, 32); memcpy(_info.Fingerprint, edgeHash, 32);
    const size_t end = url.find_first_of(L"?#");
    const std::wstring clean = url.substr(0, end);
    const std::wstring name = clean.substr(clean.find_last_of(L'/') + 1);
    for (size_t i = 0; i < name.size() && i < 160; ++i)
      _info.Name[i] = ((name[i] >= L'a' && name[i] <= L'z') || (name[i] >= L'A' && name[i] <= L'Z')
          || (name[i] >= L'0' && name[i] <= L'9') || name[i] == L'.' || name[i] == L'-' || name[i] == L'_') ? name[i] : L'_';
    if (!_info.Name[0]) memcpy(_info.Name, L"remote-archive", sizeof(L"remote-archive"));
    if (!SaveInfo(_folder, _info)) { Close(); Status = L"Cache write failed (memory only)"; return S_OK; }
    Status = rejected ? L"Old cache ignored (source changed); new cache" : L"New cache";
    Prune();
    return S_OK;
  }
  bool Read(ULONGLONG offset, unsigned length, std::vector<unsigned char> &data)
  {
    using namespace NHttpCache;
    if (!Enabled() || !_blocks.count(offset)) return false;
    std::vector<BYTE> bytes;
    CBlockHeader header;
    BYTE digest[32], identity[32];
    if (ReadFileBytes(BlockPath(offset), bytes, sizeof(header) + length) && bytes.size() == sizeof(header) + length)
    {
      memcpy(&header, bytes.data(), sizeof(header));
      if (!memcmp(header.Magic, "7ZHC01B", 8) && header.Offset == offset && header.Size == length
          && BlockIdentity(identity) && !memcmp(identity, header.Archive, 32)
          && Hash(bytes.data() + sizeof(header), length, digest) && !memcmp(digest, header.Digest, 32))
      {
        data.assign(bytes.begin() + sizeof(header), bytes.end());
        Touch(BlockPath(offset)); Touch(_folder + L"\\info.bin");
        return true;
      }
    }
    _blocks.erase(offset);
    Status = L"Damaged cache block ignored; downloading again";
    return false;
  }
  void Write(ULONGLONG offset, const std::vector<unsigned char> &data)
  {
    using namespace NHttpCache;
    if (!Enabled()) return;
    CCacheHandle manager(CreateFileW((_root + L"\\manage.lock").c_str(), GENERIC_WRITE, 0,
        NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
    if (!manager.Valid()) { Status = L"Cache busy; new bytes kept in memory"; return; }
    CBlockHeader header = {};
    memcpy(header.Magic, "7ZHC01B", 8); header.Offset = offset; header.Size = (DWORD)data.size();
    if (!Hash(data.data(), data.size(), header.Digest) || !BlockIdentity(header.Archive)) return;
    std::vector<BYTE> bytes(sizeof(header) + data.size());
    memcpy(bytes.data(), &header, sizeof(header)); memcpy(bytes.data() + sizeof(header), data.data(), data.size());
    if (!AtomicWrite(BlockPath(offset), bytes.data(), (DWORD)bytes.size()))
    { Status = L"Cache write failed (memory only for new data)"; return; }
    _blocks[offset] = (unsigned)data.size();
    Touch(_folder + L"\\info.bin");
    Prune();
  }
private:
  // Caller holds manage.lock. Active foreign entries remain untouched.
  void Prune()
  {
    using namespace NHttpCache;
    if (!_maximum) return;
    const auto catalog = CatalogRoot(_root);
    ULONGLONG used = 0;
    for (const auto &entry : catalog) used += entry.Used;
    if (used <= _maximum) return;
    struct CVictim { std::wstring Folder, Name; ULONGLONG Size, Time, Offset; bool Own; };
    std::vector<CVictim> victims;
    for (const auto &entry : catalog)
    {
      const std::wstring folder = _root + L"\\" + entry.Id;
      // Active entries are skipped; manage.lock prevents opening inactive ones.
      if (entry.Id != _id)
      {
        CCacheHandle lock(CreateFileW((folder + L"\\session.lock").c_str(), GENERIC_WRITE, 0,
            NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
        if (!lock.Valid()) continue;
        // Manager lock prevents a new session opening this entry during pruning.
      }
      for (const auto &file : Children(folder))
      {
        ULONGLONG at = 0;
        if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !BlockName(file.cFileName, at)) continue;
        CVictim victim;
        victim.Folder = folder; victim.Name = file.cFileName; victim.Own = entry.Id == _id; victim.Offset = at;
        victim.Size = ((ULONGLONG)file.nFileSizeHigh << 32) | file.nFileSizeLow;
        victim.Time = ((ULONGLONG)file.ftLastWriteTime.dwHighDateTime << 32) | file.ftLastWriteTime.dwLowDateTime;
        victims.push_back(victim);
      }
    }
    std::sort(victims.begin(), victims.end(), [](const CVictim &a, const CVictim &b) { return a.Time < b.Time; });
    for (const auto &victim : victims)
    {
      if (used <= _maximum) break;
      if (DeleteFileW((victim.Folder + L"\\" + victim.Name).c_str()))
      { used -= victim.Size; if (victim.Own) _blocks.erase(victim.Offset); }
    }
    // Remove empty inactive metadata too, so many old entries cannot grow forever.
    for (const auto &entry : catalog)
    {
      if (used <= _maximum) break;
      if (entry.Id == _id) continue;
      const std::wstring folder = _root + L"\\" + entry.Id;
      {
        CCacheHandle lock(CreateFileW((folder + L"\\session.lock").c_str(), GENERIC_WRITE, 0,
            NULL, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL));
        if (!lock.Valid()) continue;
        bool hasBlocks = false;
        for (const auto &file : Children(folder))
        {
          ULONGLONG at = 0;
          if (BlockName(file.cFileName, at)) { hasBlocks = true; break; }
        }
        if (hasBlocks) continue;
        for (const auto &file : Children(folder))
        {
          const std::wstring name(file.cFileName);
          if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && OwnedFile(name) && name != L"session.lock"
              && DeleteFileW((folder + L"\\" + name).c_str()))
            used -= (std::min)(used, ((ULONGLONG)file.nFileSizeHigh << 32) | file.nFileSizeLow);
        }
      }
      DeleteFileW((folder + L"\\session.lock").c_str()); RemoveDirectoryW(folder.c_str());
    }
    if (used > _maximum) Status = L"Cache limit reached; active entries retained";
  }
private:
  CHttpDiskCache(const CHttpDiskCache &) = delete;
  CHttpDiskCache &operator=(const CHttpDiskCache &) = delete;
public:
  CHttpDiskCache() = default;
};
}
#endif
