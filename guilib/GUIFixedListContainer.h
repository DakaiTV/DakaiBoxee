/*!
\file GUIFixedListContainer.h
\brief 
*/

#pragma once

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

#include "GUIBaseContainer.h"

/*!
 \ingroup controls
 \brief 
 */
class CGUIFixedListContainer : public CGUIBaseContainer
{
public:
  CGUIFixedListContainer(int dwParentID, int dwControlId, float posX, float posY, float width, float height, ORIENTATION orientation, int scrollTime, int preloadItems, int fixedPosition, ListSelectionMode selectionMode);
  virtual ~CGUIFixedListContainer(void);
  virtual CGUIFixedListContainer *Clone() const { return new CGUIFixedListContainer(*this); };

  virtual bool OnAction(const CAction &action);
  virtual bool OnMessage(CGUIMessage& message);
  virtual void Reset();

  void PinPosition(bool bPin);
  bool IsWrapAround();
  
protected:
  virtual void ScrollToOffset(int offset);

  virtual void Scroll(int amount);
  virtual bool MoveDown(bool wrapAround);
  virtual bool MoveUp(bool wrapAround);
  virtual void ValidateOffset();
  virtual bool SelectItemFromPoint(const CPoint &point);
  virtual void SelectItem(int item);
  virtual bool HasNextPage() const;
  virtual bool HasPreviousPage() const;
  virtual int GetCurrentPage() const;
  
  bool m_bPinnedPosition;
};

