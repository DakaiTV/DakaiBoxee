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

#include "StdString.h"
#include "Thread.h"

class CSplash : public CThread
{
public:
  CSplash(const CStdString& imageName);
  virtual ~CSplash();

  bool IsRunning();
  bool Start();
  void Stop();

  // In case you don't want to use another thread
  void Show();
  void Hide();
  
private:
  virtual void Process();
  virtual void OnStartup();
  virtual void OnExit();

  float fade;
  CStdString m_ImageName;
#ifdef HAS_DX
  D3DGAMMARAMP newRamp;
  D3DGAMMARAMP oldRamp;  
 
#endif  
};
