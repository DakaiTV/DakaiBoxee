/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */


#include "GUIDialogYesNo2.h"
#include "GUIWindowManager.h"

CGUIDialogYesNo2::CGUIDialogYesNo2(void)
    : CGUIDialogBoxBase(WINDOW_DIALOG_YES_NO_2, "DialogYesNo2.xml")
{
  m_bConfirmed = false;
}

CGUIDialogYesNo2::~CGUIDialogYesNo2()
{
}

bool CGUIDialogYesNo2::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      int iAction = message.GetParam1();
      if (1 || ACTION_SELECT_ITEM == iAction)
      {
        if (iControl == 10)
        {
          m_bConfirmed = false;
          Close();
          return true;
        }
        if (iControl == 11)
        {
          m_bConfirmed = true;
          Close();
          return true;
        }
      }
    }
    break;
  }
  return CGUIDialogBoxBase::OnMessage(message);
}

bool CGUIDialogYesNo2::OnAction(const CAction& action)
{
  if (action.id == ACTION_PREVIOUS_MENU)
  {
    m_bCanceled = true;
    m_bConfirmed = false;
    Close();
    return true;
  }

  return CGUIDialogBoxBase::OnAction(action);
}

// \brief Show CGUIDialogYesNo dialog, then wait for user to dismiss it.
// \return true if user selects Yes, false if user selects No.
bool CGUIDialogYesNo2::ShowAndGetInput(int heading, int line, bool& bCanceled)
{
  return ShowAndGetInput(heading,line,-1,-1,bCanceled);
}

bool CGUIDialogYesNo2::ShowAndGetInput(int heading, int line, int iNoLabel, int iYesLabel)
{
  bool bDummy;
  return ShowAndGetInput(heading,line,iNoLabel,iYesLabel,bDummy);
}

bool CGUIDialogYesNo2::ShowAndGetInput(int heading, int line, int iNoLabel, int iYesLabel, bool& bCanceled, unsigned int autoCloseTime, int defaultButton)
{
  CGUIDialogYesNo2 *dialog = (CGUIDialogYesNo2 *)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO_2);
  if (!dialog) return false;
  dialog->SetHeading(heading);
  dialog->SetLine(0, line);
  if (autoCloseTime)
    dialog->SetAutoClose(autoCloseTime);
  if (iNoLabel != -1)
    dialog->SetChoice(0,iNoLabel);
  if (iYesLabel != -1)
    dialog->SetChoice(1,iYesLabel);
  if (defaultButton != -1)
    dialog->SetDefaultChoice(defaultButton);
  dialog->m_bCanceled = false;
  dialog->DoModal();
  bCanceled = dialog->m_bCanceled;
  return (dialog->IsConfirmed()) ? true : false;
}

bool CGUIDialogYesNo2::ShowAndGetInput(const CStdString& heading, const CStdString& line)
{
  CStdString empty("");
  return ShowAndGetInput(heading,line,empty,empty);
}

bool CGUIDialogYesNo2::ShowAndGetInput(const CStdString& heading, const CStdString& line, int defaultButton)
{
  CStdString empty("");
  bool bDummy;
  return ShowAndGetInput(heading,line,empty,empty,bDummy,defaultButton);
}

bool CGUIDialogYesNo2::ShowAndGetInput(const CStdString& heading, const CStdString& line, bool& bCanceled)
{
  CStdString empty("");
  return ShowAndGetInput(heading,line,empty,empty,bCanceled);
}

bool CGUIDialogYesNo2::ShowAndGetInput(const CStdString& heading, const CStdString& line, const CStdString& noLabel, const CStdString& yesLabel)
{
  bool bDummy;
  return ShowAndGetInput(heading,line,noLabel,yesLabel,bDummy);
}

bool CGUIDialogYesNo2::ShowAndGetInput(const CStdString& heading, const CStdString& line, const CStdString& noLabel, const CStdString& yesLabel, bool& bCanceled, int defaultButton)
{
  CGUIDialogYesNo2 *dialog = (CGUIDialogYesNo2 *)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO_2);
  if (!dialog) return false;
  dialog->SetHeading(heading);
  dialog->SetLine(0, line);
  dialog->m_bCanceled = false;
  if (noLabel != "")
    dialog->SetChoice(0,noLabel);
  if (yesLabel != "")
    dialog->SetChoice(1,yesLabel);
  if (defaultButton != -1)
    dialog->SetDefaultChoice(defaultButton);
  dialog->DoModal();
  bCanceled = dialog->m_bCanceled;
  return (dialog->IsConfirmed()) ? true : false;
}
