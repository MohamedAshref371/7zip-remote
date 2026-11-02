// GUI-only localization for the remote extension. Transport/CLI text stays canonical.
#ifndef ZIP7_INC_REMOTE_LANG_H
#define ZIP7_INC_REMOTE_LANG_H

#include "LangUtils.h"
#include "../../../Windows/Window.h"
#include <wchar.h>

inline UString RemoteLang(const wchar_t *english)
{
#if defined(Z7_HTTP) && defined(Z7_LANG)
  struct CEntry { UInt32 Id; const wchar_t *English; };
  static const CEntry entries[] = {
#define REMOTE_LANG(id, text) { id, text },
#include "RemoteLangStrings.inc"
#undef REMOTE_LANG
  };
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(entries); ++i)
    if (wcscmp(english, entries[i].English) == 0)
    {
      UString translated;
      LangString_OnlyFromLangFile(entries[i].Id, translated);
      if (!translated.IsEmpty()) return translated;
      break;
    }
#endif
  return UString(english);
}

// Expand placeholders in one pass: inserted values are never interpreted as tokens.
inline UString RemoteLangFormat(const wchar_t *english, const UString &first,
    const UString &second = UString())
{
  const UString format = RemoteLang(english);
  UString result;
  for (unsigned i = 0; i < format.Len(); ++i)
  {
    if (i + 2 < format.Len() && format[i] == L'{' && format[i + 2] == L'}'
        && (format[i + 1] == L'0' || format[i + 1] == L'1'))
    {
      result += format[i + 1] == L'0' ? first : second;
      i += 2;
    }
    else result += format[i];
  }
  return result;
}

inline UString RemoteLangMessage(const wchar_t *english)
{
#ifdef Z7_HTTP
  // Preserve the HTTP status appended by CRemoteArchiveSource::ErrorMessage.
  const UString full(english);
  const int suffix = full.ReverseFind(L'(');
  if (suffix > 0 && full[(unsigned)suffix - 1] == L' '
      && full.Len() == (unsigned)suffix + 10
      && wcsncmp(english + suffix, L"(HTTP ", 6) == 0
      && full.Back() == L')'
      && english[suffix + 6] >= L'0' && english[suffix + 6] <= L'9'
      && english[suffix + 7] >= L'0' && english[suffix + 7] <= L'9'
      && english[suffix + 8] >= L'0' && english[suffix + 8] <= L'9')
    return RemoteLangMessage(full.Left((unsigned)suffix - 1)) + full.Ptr((unsigned)suffix - 1);
  // This is the sole parameterized transport diagnostic. Require exactly 3 digits;
  // do not translate arbitrary paths, URLs or fragments of external error messages.
  const wchar_t prefix[] = L"Unexpected HTTP status ";
  const unsigned n = Z7_ARRAY_SIZE(prefix) - 1;
  const UString message(english);
  if (message.Len() >= n + 3 && wcsncmp(english, prefix, n) == 0
      && english[n] >= L'0' && english[n] <= L'9'
      && english[n + 1] >= L'0' && english[n + 1] <= L'9'
      && english[n + 2] >= L'0' && english[n + 2] <= L'9'
      && wcscmp(english + n + 3, L". Use a direct archive download URL that supports partial downloads.") == 0)
    return RemoteLangFormat(L"Unexpected HTTP status {0}. Use a direct archive download URL that supports partial downloads.",
        message.Mid(n, 3));
#endif
  return RemoteLang(english);
}

inline void RemoteLangDialog(HWND dialog)
{
#ifdef Z7_HTTP
#ifdef Z7_LANG
  LangSetDlgItems(dialog, NULL, 0); // Standard OK / Cancel buttons.
#endif
  wchar_t text[2048]; // All catalog captions fit; ignore longer external text.
  if (GetWindowTextLengthW(dialog) < (int)Z7_ARRAY_SIZE(text))
  {
    GetWindowTextW(dialog, text, Z7_ARRAY_SIZE(text));
    NWindows::MySetWindowText(dialog, RemoteLang(text));
  }
  for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    if (GetWindowTextLengthW(child) < (int)Z7_ARRAY_SIZE(text))
    {
      GetWindowTextW(child, text, Z7_ARRAY_SIZE(text));
      NWindows::MySetWindowText(child, RemoteLang(text));
    }
#else
  (void)dialog;
#endif
}
#endif
