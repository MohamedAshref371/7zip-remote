// AboutDialog.cpp

#include "StdAfx.h"
#include "RemoteLang.h"

#include "../../../../C/CpuArch.h"

#include "../../MyVersion.h"
#include "../../RemoteVersion.h"

#include "../Common/LoadCodecs.h"

#include "AboutDialog.h"
#include "PropertyNameRes.h"

#include "HelpUtils.h"
#include "LangUtils.h"

#ifdef Z7_LANG
static const UInt32 kLangIDs[] =
{
  IDT_ABOUT_INFO
};
#endif

#define kHomePageURL TEXT("https://www.7-zip.org/")
#define kHelpTopic "start.htm"

#define LLL_(quote) L##quote
#define LLL(quote) LLL_(quote)

extern CCodecs *g_CodecsObj;

bool CAboutDialog::OnInit()
{
  #ifdef Z7_EXTERNAL_CODECS
  if (g_CodecsObj)
  {
    UString s;
    g_CodecsObj->GetCodecsErrorMessage(s);
    if (!s.IsEmpty())
      MessageBoxW(GetParent(), s, L"7-Zip", MB_ICONERROR);
  }
  #endif

  #ifdef Z7_LANG
  LangSetWindowText(*this, IDD_ABOUT);
  LangSetDlgItems(*this, kLangIDs, Z7_ARRAY_SIZE(kLangIDs));
  #endif
 #ifdef Z7_HTTP
  SetText(RemoteLang(L"About 7-Zip Remote"));
  SetItemText(IDT_ABOUT_VERSION, L"7-Zip Remote " LLL(REMOTE_ARCHIVE_VERSION));
  SetItemText(IDT_ABOUT_DATE, RemoteLangFormat(L"Based on 7-Zip {0}", UString(MY_VERSION_CPU)));
  SetItemText(IDT_ABOUT_REMOTE, RemoteLangFormat(L"7-Zip engine date: {0}\nDeveloped with OpenAI Codex", UString(MY_DATE)));
 #else
  SetItemText(IDT_ABOUT_VERSION, UString("7-Zip " MY_VERSION_CPU));
  SetItemText(IDT_ABOUT_DATE, LLL(MY_DATE));
 #endif
  
  NormalizePosition();
  return CModalDialog::OnInit();
}

void CAboutDialog::OnHelp()
{
  ShowHelpWindow(kHelpTopic);
}

bool CAboutDialog::OnButtonClicked(unsigned buttonID, HWND buttonHWND)
{
  LPCTSTR url;
  switch (buttonID)
  {
    case IDB_ABOUT_HOMEPAGE: url = kHomePageURL; break;
    default:
      return CModalDialog::OnButtonClicked(buttonID, buttonHWND);
  }

  #ifdef UNDER_CE
  SHELLEXECUTEINFO s;
  memset(&s, 0, sizeof(s));
  s.cbSize = sizeof(s);
  s.lpFile = url;
  ::ShellExecuteEx(&s);
  #else
  ::ShellExecute(NULL, NULL, url, NULL, NULL, SW_SHOWNORMAL);
  #endif

  return true;
}
