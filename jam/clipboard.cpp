#include "clipboard.h"

#include <windows.h>

void copy_to_windows_clipboard(const std::string& text)
  {
  if (!OpenClipboard(NULL))
    return;
  if (!EmptyClipboard())
    return;

  HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);

  if (!hGlob)
    {
    CloseClipboard();
    return;
    }

  memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);

  GlobalUnlock(hGlob);

  SetClipboardData(CF_TEXT, hGlob);
  CloseClipboard();
  GlobalFree(hGlob);
  }


std::string get_text_from_windows_clipboard()
  {
  if (!OpenClipboard(NULL))
    return std::string();
  HGLOBAL hglb = GetClipboardData(CF_TEXT);
  if (hglb != NULL)
    {
    LPTSTR lptstr = (LPTSTR)GlobalLock(hglb);
    if (lptstr != NULL)
      {
      std::string output(lptstr);
      GlobalUnlock(hglb);
      CloseClipboard();
      return output;
      }
    }
  CloseClipboard();
  return std::string();
  }