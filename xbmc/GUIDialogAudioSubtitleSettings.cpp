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

#include "system.h"
#include "GUIDialogAudioSubtitleSettings.h"
#include "GUIDialogFileBrowser.h"
#include "GUIPassword.h"
#include "Util.h"
#include "Application.h"
#include "VideoDatabase.h"
#include "XBAudioConfig.h"
#include "GUIDialogYesNo.h"
#include "FileSystem/Directory.h"
#include "FileSystem/File.h"
#include "URL.h"
#include "FileItem.h"
#include "SkinInfo.h"
#include "Settings.h"
#include "AdvancedSettings.h"
#include "GUISettings.h"
#include "LocalizeStrings.h"

using namespace std;
using namespace XFILE;
using namespace DIRECTORY;

#ifdef HAS_VIDEO_PLAYBACK
extern void xbox_audio_switch_channel(int iAudioStream, bool bAudioOnAllSpeakers); //lowlevel audio
#endif

CGUIDialogAudioSubtitleSettings::CGUIDialogAudioSubtitleSettings(void)
    : CGUIDialogSettings(WINDOW_DIALOG_AUDIO_OSD_SETTINGS, "VideoOSDSettings.xml")
{
}

CGUIDialogAudioSubtitleSettings::~CGUIDialogAudioSubtitleSettings(void)
{
}

#ifdef NEW_AUDIO_OSD
#define AUDIO_SETTINGS_VOLUME             1
#define AUDIO_SETTINGS_VOLUME_AMPLIFICATION 2
#define AUDIO_SETTINGS_DELAY              3
#define AUDIO_SETTINGS_STREAM             4
#define AUDIO_SETTINGS_OUTPUT_TO_ALL_SPEAKERS 5
#define AUDIO_SETTINGS_DIGITAL_ANALOG 6
#else
#define AUDIO_SETTINGS_DELAY              3
#define AUDIO_SETTINGS_STREAM             4
#endif

// separator 7
#define AUDIO_SETTINGS_MAKE_DEFAULT      12

void CGUIDialogAudioSubtitleSettings::CreateSettings()
{
  m_usePopupSliders = g_SkinInfo.HasSkinFile("DialogSlider.xml");

  // clear out any old settings
  m_settings.clear();

#ifdef NEW_AUDIO_OSD

  // create our settings
  m_volume = g_stSettings.m_nVolumeLevel * 0.01f;
  AddSlider(AUDIO_SETTINGS_VOLUME, 13376, &m_volume, VOLUME_MINIMUM * 0.01f, (VOLUME_MAXIMUM - VOLUME_MINIMUM) * 0.0001f, VOLUME_MAXIMUM * 0.01f, FormatDecibel, false);
#if 0
  AddSlider(AUDIO_SETTINGS_VOLUME_AMPLIFICATION, 660, &g_stSettings.m_currentVideoSettings.m_VolumeAmplification, VOLUME_DRC_MINIMUM * 0.01f, (VOLUME_DRC_MAXIMUM - VOLUME_DRC_MINIMUM) * 0.0005f, VOLUME_DRC_MAXIMUM * 0.01f, FormatDecibel, false);
#endif
  AddSlider(AUDIO_SETTINGS_DELAY, 297, &g_stSettings.m_currentVideoSettings.m_AudioDelay, -g_advancedSettings.m_videoAudioDelayRange, .025f, g_advancedSettings.m_videoAudioDelayRange, FormatDelay);
  AddAudioStreams(AUDIO_SETTINGS_STREAM);

  // only show stuff available in digital mode if we have digital output
  if(g_audioConfig.HasDigitalOutput())
  {
    AddBool(AUDIO_SETTINGS_OUTPUT_TO_ALL_SPEAKERS, 252, &g_stSettings.m_currentVideoSettings.m_OutputToAllSpeakers, g_guiSettings.GetInt("audiooutput.mode") == AUDIO_DIGITAL);

    int settings[2] = { 338, 339 }; //ANALOG, DIGITAL
    m_outputmode = g_guiSettings.GetInt("audiooutput.mode");
    AddSpin(AUDIO_SETTINGS_DIGITAL_ANALOG, 337, &m_outputmode, 2, settings);
  }

  AddButton(AUDIO_SETTINGS_MAKE_DEFAULT, 12376);

#else
  AddSlider(AUDIO_SETTINGS_DELAY, 297, &g_stSettings.m_currentVideoSettings.m_AudioDelay, -g_advancedSettings.m_videoAudioDelayRange, .025f, g_advancedSettings.m_videoAudioDelayRange, FormatDelay);
  AddAudioStreams(AUDIO_SETTINGS_STREAM);
#endif
}

void CGUIDialogAudioSubtitleSettings::AddAudioStreams(unsigned int id)
{
  SettingInfo setting;
  setting.id = id;
  setting.name = g_localizeStrings.Get(460);
  setting.type = SettingInfo::SPIN;
  setting.min = 0;
  setting.data = &m_audioStream;

  // get the number of audio strams for the current movie
  setting.max = (float)g_application.m_pPlayer->GetAudioStreamCount() - 1;
  m_audioStream = g_application.m_pPlayer->GetAudioStream();

  if( m_audioStream < 0 ) m_audioStream = 0;

  // check if we have a single, stereo stream, and if so, allow us to split into
  // left, right or both
  if (!setting.max)
  {
    CStdString strAudioInfo;
    g_application.m_pPlayer->GetAudioInfo(strAudioInfo);
    int iNumChannels = atoi(strAudioInfo.Right(strAudioInfo.size() - strAudioInfo.Find("chns:") - 5).c_str());
    CStdString strAudioCodec = strAudioInfo.Mid(7, strAudioInfo.Find(") VBR") - 5);
    bool bDTS = strstr(strAudioCodec.c_str(), "DTS") != 0;
    bool bAC3 = strstr(strAudioCodec.c_str(), "AC3") != 0;
    if (iNumChannels == 2 && !(bDTS || bAC3))
    { // ok, enable these options
/*      if (g_stSettings.m_currentVideoSettings.m_AudioStream == -1)
      { // default to stereo stream
        g_stSettings.m_currentVideoSettings.m_AudioStream = 0;
      }*/
      setting.max = 2;
      for (int i = 0; i <= setting.max; i++)
        setting.entry.push_back(g_localizeStrings.Get(13320 + i));
      m_audioStream = -g_stSettings.m_currentVideoSettings.m_AudioStream - 1;
      m_settings.push_back(setting);
      return;
    }
  }

  // cycle through each audio stream and add it to our list control
  for (int i = 0; i <= setting.max; ++i)
  {
    CStdString strItem;
    CStdString strName;
    g_application.m_pPlayer->GetAudioStreamName(i, strName);
    if (strName.length() == 0)
      strName = "Unnamed";

    strItem.Format("%s (%i/%i)", strName.c_str(), i + 1, (int)setting.max + 1);
    setting.entry.push_back(strItem);
  }

  if( setting.max < 0 )
  {
    setting.max = 0;
    setting.entry.push_back(g_localizeStrings.Get(231).c_str());
  }

  m_settings.push_back(setting);
}

void CGUIDialogAudioSubtitleSettings::OnSettingChanged(SettingInfo &setting)
{
#ifdef NEW_AUDIO_OSD
  // check and update anything that needs it
  if (setting.id == AUDIO_SETTINGS_VOLUME)
  {
    g_stSettings.m_nVolumeLevel = (long)(m_volume * 100.0f);
    g_application.SetVolume(int(((float)(g_stSettings.m_nVolumeLevel - VOLUME_MINIMUM)) / (VOLUME_MAXIMUM - VOLUME_MINIMUM)*100.0f + 0.5f));
  }
  else if (setting.id == AUDIO_SETTINGS_VOLUME_AMPLIFICATION)
  {
    if (g_application.m_pPlayer)
      g_application.m_pPlayer->SetDynamicRangeCompression((long)(g_stSettings.m_currentVideoSettings.m_VolumeAmplification * 100));
  }
  else if (setting.id == AUDIO_SETTINGS_DELAY)
  {
    if (g_application.m_pPlayer)
      g_application.m_pPlayer->SetAVDelay(g_stSettings.m_currentVideoSettings.m_AudioDelay);
  }
  else if (setting.id == AUDIO_SETTINGS_STREAM)
  {
    // first check if it's a stereo track that we can change between stereo, left and right
    if (g_application.m_pPlayer->GetAudioStreamCount() == 1)
    {
      if (setting.max == 2)
      { // we're in the case we want - call the code to switch channels etc.
        // update the screen setting...
        g_stSettings.m_currentVideoSettings.m_AudioStream = -1 - m_audioStream;
        // call monkeyh1's code here...
        //bool bAudioOnAllSpeakers = (g_guiSettings.GetInt("audiooutput.mode") == AUDIO_DIGITAL) && g_stSettings.m_currentVideoSettings.m_OutputToAllSpeakers;
        return;
      }
    }
    // only change the audio stream if a different one has been asked for
    if (g_application.m_pPlayer->GetAudioStream() != m_audioStream)
    {
      g_stSettings.m_currentVideoSettings.m_AudioStream = m_audioStream;
      g_application.m_pPlayer->SetAudioStream(m_audioStream);    // Set the audio stream to the one selected
    }
  }
  else if (setting.id == AUDIO_SETTINGS_OUTPUT_TO_ALL_SPEAKERS)
  {
    g_application.Restart();
  }
  else if (setting.id == AUDIO_SETTINGS_DIGITAL_ANALOG)
  {
    if(m_outputmode == 0) // might be unneccesary (indexes match), but just for clearity
      g_guiSettings.SetInt("audiooutput.mode", AUDIO_ANALOG);
    else
      g_guiSettings.SetInt("audiooutput.mode", AUDIO_DIGITAL);

    EnableSettings(AUDIO_SETTINGS_OUTPUT_TO_ALL_SPEAKERS, g_guiSettings.GetInt("audiooutput.mode") == AUDIO_DIGITAL);
    g_application.Restart();
  }
  else if (setting.id == AUDIO_SETTINGS_MAKE_DEFAULT)
  {
    if (g_settings.m_vecProfiles[g_settings.m_iLastLoadedProfileIndex].settingsLocked() &&
        g_settings.m_vecProfiles[0].getLockMode() != ::LOCK_MODE_EVERYONE)
      if (!g_passwordManager.IsMasterLockUnlocked(true))
        return;

    // prompt user if they are sure
    if (CGUIDialogYesNo::ShowAndGetInput(12376, 750, 0, 12377))
    { // reset the settings
      CVideoDatabase db;
      db.Open();
      db.EraseVideoSettings();
      db.Close();
      g_stSettings.m_defaultVideoSettings = g_stSettings.m_currentVideoSettings;
      g_settings.Save();
    }
  }
#else
  if (setting.id == AUDIO_SETTINGS_DELAY)
  {
    if (g_application.m_pPlayer)
      g_application.m_pPlayer->SetAVDelay(g_stSettings.m_currentVideoSettings.m_AudioDelay);
  }
  else if (setting.id == AUDIO_SETTINGS_STREAM)
  {
    // first check if it's a stereo track that we can change between stereo, left and right
    if (g_application.m_pPlayer->GetAudioStreamCount() == 1)
    {
      if (setting.max == 2)
      { // we're in the case we want - call the code to switch channels etc.
        // update the screen setting...
        g_stSettings.m_currentVideoSettings.m_AudioStream = -1 - m_audioStream;
        // call monkeyh1's code here...
        //bool bAudioOnAllSpeakers = (g_guiSettings.GetInt("audiooutput.mode") == AUDIO_DIGITAL) && g_stSettings.m_currentVideoSettings.m_OutputToAllSpeakers;
        return;
      }
    }
    // only change the audio stream if a different one has been asked for
    if (g_application.m_pPlayer->GetAudioStream() != m_audioStream)
    {
      g_stSettings.m_currentVideoSettings.m_AudioStream = m_audioStream;
      g_application.m_pPlayer->SetAudioStream(m_audioStream);    // Set the audio stream to the one selected
    }
  }
#endif
}

void CGUIDialogAudioSubtitleSettings::Render()
{
  m_volume = g_stSettings.m_nVolumeLevel * 0.01f;
#ifdef  NEW_AUDIO_OSD
  UpdateSetting(AUDIO_SETTINGS_VOLUME);
#endif
  if (g_application.m_pPlayer)
  {
    // these settings can change on the fly
    UpdateSetting(AUDIO_SETTINGS_DELAY);
  }
  CGUIDialogSettings::Render();
}

CStdString CGUIDialogAudioSubtitleSettings::FormatDecibel(float value, float interval)
{
  CStdString text;
  text.Format("%2.1f dB", value);
  return text;
}

CStdString CGUIDialogAudioSubtitleSettings::FormatDelay(float value, float interval)
{
  CStdString text;
  if (fabs(value) < 0.5f*interval)
    text = "0.000s";
  else if (value < 0)
    text.Format(g_localizeStrings.Get(22004).c_str(), fabs(value));
  else
    text.Format(g_localizeStrings.Get(22005).c_str(), value);
  return text;
}
