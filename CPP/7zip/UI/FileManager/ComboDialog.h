// ComboDialog.h

#ifndef ZIP7_INC_COMBO_DIALOG_H
#define ZIP7_INC_COMBO_DIALOG_H

#include "../../../Windows/Control/ComboBox.h"
#include "../../../Windows/Control/Dialog.h"

#include "ComboDialogRes.h"

class CComboDialog: public NWindows::NControl::CModalDialog
{
  NWindows::NControl::CComboBox _comboBox;
  virtual void OnOK() Z7_override;
  virtual bool OnInit() Z7_override;
  virtual bool OnSize(WPARAM wParam, int xSize, int ySize) Z7_override;
public:
  // bool Sorted;
  UString Title;
  UString Static;
  UString Value;
  UStringVector Strings;
  bool RightToLeft = false; // Set only for the remote URL renewal dialog.
  
  // CComboDialog(): Sorted(false) {};
  INT_PTR Create(HWND parentWindow = NULL) { return CModalDialog::Create(RightToLeft ? IDD_COMBO_RTL : IDD_COMBO, parentWindow); }
};

#endif
