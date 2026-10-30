// FileFolderPluginOpen.h

#ifndef ZIP7_INC_FILE_FOLDER_PLUGIN_OPEN_H
#define ZIP7_INC_FILE_FOLDER_PLUGIN_OPEN_H

#include "../../../Windows/DLL.h"
#ifdef Z7_HTTP
#include "../../../Windows/HttpDiskCache.h"
#endif

struct CFfpOpen
{
  Z7_CLASS_NO_COPY(CFfpOpen)
public:
  // out:
  bool Encrypted;
  #ifdef Z7_HTTP
  bool SkipHttpETag = false;
  NWindows::CHttpDiskCacheOptions HttpCacheOptions;
  #endif
  UString Password;

  NWindows::NDLL::CLibrary Library;
  CMyComPtr<IFolderFolder> Folder;
  UString ErrorMessage;

  CFfpOpen(): Encrypted (false) {}

  HRESULT OpenFileFolderPlugin(IInStream *inStream,
      const FString &path, const UString &arcFormat, HWND parentWindow);
};


#endif
