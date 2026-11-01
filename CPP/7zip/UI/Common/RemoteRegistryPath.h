#ifndef ZIP7_INC_REMOTE_REGISTRY_PATH_H
#define ZIP7_INC_REMOTE_REGISTRY_PATH_H

// HTTP-enabled builds are distributed as 7-Zip Remote. Keep their preferences
// separate from an installed upstream 7-Zip and from local-only builds.
#ifdef Z7_HTTP
  #define Z7_REGISTRY_ROOT TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("7-Zip Remote")
  #define Z7_REMOTE_CACHE_REGISTRY_PATH L"Software\\7-Zip Remote\\FM\\RemoteCache"
#else
  #define Z7_REGISTRY_ROOT TEXT("Software") TEXT(STRING_PATH_SEPARATOR) TEXT("7-Zip")
  #define Z7_REMOTE_CACHE_REGISTRY_PATH L"Software\\7-Zip\\FM\\RemoteCache"
#endif

#endif
