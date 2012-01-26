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

// FileShoutcast.h: interface for the CFileShoutcast class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FILESHOUTCAST_H__6B6082E6_547E_44C4_8801_9890781659C0__INCLUDED_)
#define AFX_FILESHOUTCAST_H__6B6082E6_547E_44C4_8801_9890781659C0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "IFile.h"
#include "StdString.h"

// prevent inclusion of config.h from libshout
#define __SRCONFIG_H__
#include "lib/libshout/rip_manager.h"
#undef __SRCONFIG_H__

namespace MUSIC_INFO
{
  class CMusicInfoTag;
}

namespace XFILE
{
typedef struct FileStateSt
{
  bool bBuffering;
  bool bRipDone;
  bool bRipStarted;
  bool bRipError;
}
FileState;

class CFileShoutcast : public IFile
{
public:
  CFileShoutcast();
  virtual ~CFileShoutcast();
  virtual int64_t GetPosition();
  virtual int64_t GetLength();
  virtual bool Open(const CURI& url);
  virtual bool Exists(const CURI& url) { return true;};
  virtual int Stat(const CURI& url, struct __stat64* buffer) { errno = ENOENT; return -1; };
  virtual unsigned int Read(void* lpBuf, int64_t uiBufSize);
  virtual int64_t Seek(int64_t iFilePosition, int iWhence = SEEK_SET);
  virtual void Close();
  virtual bool CanRecord();
  virtual bool Record();
  virtual void StopRecording();
  virtual bool IsRecording();
  virtual bool GetMusicInfoTag(MUSIC_INFO::CMusicInfoTag& tag);
  virtual CStdString GetContent();
protected:
  void outputTimeoutMessage(const char* message);
  unsigned int m_lastTime;
  int m_contenttype;
  RIP_MANAGER_OPTIONS *m_opt;
};
}
#endif // !defined(AFX_FILESHOUTCAST_H__6B6082E6_547E_44C4_8801_9890781659C0__INCLUDED_)
