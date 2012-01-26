/*
* XBMC Media Center
* Copyright (c) 2002 d7o3g4q and RUNTiME
* Portions Copyright (c) by the authors of ffmpeg and xvid
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// AsyncAudioRenderer.h: interface for the CAsyncDirectSound class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_IAUDIORENDERER_H__B590A94D_D15E_43A6_A41D_527BD441B5F5__INCLUDED_)
#define AFX_IAUDIORENDERER_H__B590A94D_D15E_43A6_A41D_527BD441B5F5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "IAudioCallback.h"
extern void RegisterAudioCallback(IAudioCallback* pCallback);
extern void UnRegisterAudioCallback();

class IAudioRenderer
{
public:
  IAudioRenderer() {};
  virtual ~IAudioRenderer() {};
  virtual bool Initialize(IAudioCallback* pCallback, int iChannels, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, const char* strAudioCodec, bool bIsMusic, bool bPassthrough) = 0;
  virtual void UnRegisterAudioCallback() = 0;
  virtual void RegisterAudioCallback(IAudioCallback* pCallback) = 0;
  virtual float GetDelay() = 0;
  virtual float GetCacheTime() = 0;

  virtual unsigned int AddPackets(const void* data, unsigned int len) = 0;
  virtual bool IsResampling() { return false;};
  virtual unsigned int GetSpace() = 0;
  virtual bool Deinitialize() = 0;
  virtual bool Pause() = 0;
  virtual bool Stop() = 0;
  virtual bool Resume() = 0;
  virtual unsigned int GetChunkLen() = 0;

  virtual long GetCurrentVolume() const = 0;
  virtual void Mute(bool bMute) = 0;
  virtual bool SetCurrentVolume(long nVolume) = 0;
  virtual void SetDynamicRangeCompression(long drc) {};
  virtual int SetPlaySpeed(int iSpeed) = 0;
  virtual void WaitCompletion() = 0;
  virtual void SwitchChannels(int iAudioStream, bool bAudioOnAllSpeakers) = 0;

private:
};

#endif // !defined(AFX_IAUDIORENDERER_H__B590A94D_D15E_43A6_A41D_527BD441B5F5__INCLUDED_)