// OpenCallback.h

#ifndef ZIP7_INC_OPEN_CALLBACK_H
#define ZIP7_INC_OPEN_CALLBACK_H

#include "../Common/ArchiveOpenCallback.h"

#ifdef Z7_SFX
#include "ProgressDialog.h"
#else
#include "ProgressDialog2.h"
#endif

/* we can use IArchiveOpenCallback or IOpenCallbackUI here */

class COpenArchiveCallback Z7_final:
  /*
  public IArchiveOpenCallback,
  public IProgress,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
  */
  public IOpenCallbackUI
{
  // NWindows::NSynchronization::CCriticalSection _criticalSection;
public:
  bool PasswordIsDefined;
  bool PasswordWasAsked;
  UString Password;
  HWND ParentWindow;
  CProgressDialog ProgressDialog;

  /*
  Z7_COM_UNKNOWN_IMP_3(
    IArchiveOpenVolumeCallback,
    IProgress
    ICryptoGetTextPassword
    )

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  // ICryptoGetTextPassword
  Z7_COM7F_IMP(CryptoGetTextPassword(BSTR *password))
  */

  Z7_IFACE_IMP(IOpenCallbackUI)
 #if defined(Z7_HTTP) && !defined(Z7_SFX)
  HRESULT Open_NetworkProgress(UInt64 downloaded, UInt64 size, UInt64 requests) Z7_override
  {
    ProgressDialog.Sync.Set_NetworkProgress(downloaded, size, requests);
    return S_OK;
  }
 #endif

  COpenArchiveCallback():
      ParentWindow(NULL)
  {
    // _subArchiveMode = false;
    PasswordIsDefined = false;
    PasswordWasAsked = false;
  }
  /*
  void Init()
  {
    PasswordIsDefined = false;
    _subArchiveMode = false;
  }
  */
  
  INT_PTR StartProgressDialog(const UString &title, NWindows::CThread &thread)
  {
    return ProgressDialog.Create(title, thread, ParentWindow);
  }
};

#endif
