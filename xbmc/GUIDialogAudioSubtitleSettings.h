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

#include "GUIDialogSettings.h"
#include "FileItem.h"

class CGUIDialogAudioSubtitleSettings :
      public CGUIDialogSettings
{
public:
  CGUIDialogAudioSubtitleSettings(void);
  virtual ~CGUIDialogAudioSubtitleSettings(void);
  virtual void Render();

  static CStdString FormatDelay(float value, float minimum);
  static CStdString FormatDecibel(float value, float minimum);
  
protected:
  virtual void CreateSettings();
  virtual void OnSettingChanged(SettingInfo &setting);
  virtual void SetAudioTitle();
  void SetSubtitleVisibility();
  void SetItemSelected(int controlList, int item);
  void OnBrowseSubtitleSelect();

  void AddAudioStreams(unsigned int id);
  void AddSubtitleStreams(unsigned int id);

  CFileItemList m_audioStreamsList;
  CFileItemList m_subtitleStreamsList;
  float m_volume;
  int m_audioStream;
  int m_subtitleStream;
  int m_browseSubtitleIndex;
  int m_outputmode;
  bool m_subtitleVisible;
};
