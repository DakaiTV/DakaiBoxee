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
#include "VirtualDirectory.h"
#include "FactoryDirectory.h"
#include "Settings.h"
#include "Util.h"
#include "Profile.h"
#include "Directory.h"
#include "DirectoryCache.h"
#include "../MediaManager.h"
#include "File.h"
#include "FileItem.h"
#ifdef _WIN32
#include "WIN32Util.h"
#endif

#include "utils/log.h"

using namespace XFILE;

namespace DIRECTORY
{

CVirtualDirectory::CVirtualDirectory(void) : m_vecSources(NULL)
{
  m_allowPrompting = true;  // by default, prompting is allowed.
  m_cacheDirectory = DIR_CACHE_ONCE;  // by default, caching is done.
  m_allowNonLocalSources = true;
}

CVirtualDirectory::~CVirtualDirectory(void)
{}

/*!
 \brief Add shares to the virtual directory
 \param VECSOURCES Shares to add
 \sa CMediaSource, VECSOURCES
 */
void CVirtualDirectory::SetSources(VECSOURCES& vecSources)
{
  m_vecSources = &vecSources;
}

/*!
 \brief Retrieve the shares or the content of a directory.
 \param strPath Specifies the path of the directory to retrieve or pass an empty string to get the shares.
 \param items Content of the directory.
 \return Returns \e true, if directory access is successfull.
 \note If \e strPath is an empty string, the share \e items have thumbnails and icons set, else the thumbnails
    and icons have to be set manually.
 */

bool CVirtualDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
  return GetDirectory(strPath,items,true);
}
bool CVirtualDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items, bool bUseFileDirectories)
{
  if (!m_vecSources)
  {
    items.m_strPath=strPath;
    return true;
  }

  VECSOURCES shares;
  GetSources(shares);
  if (!strPath.IsEmpty() && strPath != "files://")
  {
    bool bIsSourceName = false;
    int iIndex = CUtil::GetMatchingSource(strPath, shares, bIsSourceName);
    // added exception for various local hd items
    // function doesn't work for http/shout streams with options..
#ifdef _LINUX
    if (iIndex > -1 || strPath.Mid(0, 1) == "/" || strPath.Mid(1, 1) == ":" 
#else
    if (iIndex > -1 || strPath.Mid(1, 1) == ":" 
#endif
      || strPath.Left(8).Equals("shout://") 
      || strPath.Left(8).Equals("https://") 
      || strPath.Left(7).Equals("http://") 
      || strPath.Left(7).Equals("daap://")
      || strPath.Left(9).Equals("tuxbox://")
      || strPath.Left(9).Equals("lastfm://")
      || strPath.Left(7).Equals("upnp://")
      || strPath.Left(10).Equals("musicdb://")
      || strPath.Left(10).Equals("boxeedb://")
      || strPath.Left(6).Equals("rss://")
      || strPath.Left(7).Equals("feed://")
      || strPath.Left(9).Equals("plugin://")
      || strPath.Left(7).Equals("apps://")
      || strPath.Left(14).Equals("musicsearch://")
      || strPath.Left(6).Equals("smb://")
      || strPath.Left(6).Equals("afp://")
      || strPath.Left(6).Equals("bms://")
      || strPath.Left(6).Equals("nfs://"))
    {
      // Only cache directory we are getting now
      if (!strPath.Left(7).Equals("lastfm:") && !strPath.Left(8).Equals("shout://") && !strPath.Left(9).Equals("tuxbox://") && 
    		  !strPath.Left(6).Equals("rss://") && !strPath.Left(9).Equals("plugin://") && !strPath.Left(7).Equals("feed://") && 
    		  !strPath.Left(10).Equals("boxeedb://") && !strPath.Left(6).Equals("smb://") && !strPath.Left(6).Equals("afp://") &&
    		  !strPath.Left(6).Equals("bms://") && !strPath.Left(6).Equals("nfs://"))
      {
        g_directoryCache.Clear();
      }
      bool bRetVal = CDirectory::GetDirectory(strPath, items, m_strFileMask, bUseFileDirectories, m_allowPrompting, m_cacheDirectory);
      return bRetVal;
    }

    // what do with an invalid path?
    // return false so the calling window can deal with the error accordingly
    // otherwise the root listing is returned which seems incorrect but was the previous behaviour
    CLog::Log(LOGERROR,"CVirtualDirectory::GetDirectory(%s) matches no valid source, getting root source list instead", strPath.c_str());
    return false;
  }

  // if strPath is blank, clear the list (to avoid parent items showing up)
  if (strPath.IsEmpty())
    items.Clear();

  // return the root listing
  items.m_strPath=strPath;

  // grab our shares
  for (unsigned int i = 0; i < shares.size(); ++i)
  {
    CMediaSource& share = shares[i];
    CFileItemPtr pItem(new CFileItem(share));
    if (pItem->IsLastFM() || pItem->IsShoutCast() || (pItem->m_strPath.Left(14).Equals("musicsearch://")))
      pItem->SetCanQueue(false);
    CStdString strPathUpper = pItem->m_strPath;
    strPathUpper.ToUpper();

    CStdString strIcon=share.m_strThumbnailImage;
    if (share.m_strThumbnailImage.IsEmpty())
    {
      // We have the real DVD-ROM, set icon on disktype
      if (share.m_iDriveType == CMediaSource::SOURCE_TYPE_DVD)
      {
        CUtil::GetDVDDriveIcon( pItem->m_strPath, strIcon );
        // CDetectDVDMedia::SetNewDVDShareUrl() caches disc thumb as Z:\dvdicon.tbn
        CStdString strThumb = "special://temp/dvdicon.tbn";
        if (XFILE::CFile::Exists(strThumb))
          pItem->SetThumbnailImage(strThumb);
      }
      else if (strPathUpper.Left(11) == "SOUNDTRACK:")
      strIcon = "DefaultHardDisk.png";
    else if (pItem->IsLastFM()
          || pItem->IsShoutCast()
          || pItem->IsVideoDb()
          || pItem->IsMusicDb()
          || pItem->IsPlugin()
          || pItem->IsPluginRoot()
          || pItem->m_strPath == "special://musicplaylists/"
          || pItem->m_strPath == "special://videoplaylists/"
          || pItem->m_strPath == "musicsearch://")
      strIcon = "DefaultFolder.png";
      else if (strPathUpper.Left(4) == "RSS:")
        strIcon = "defaultRSS.png";
      else if (pItem->IsRemote())
      strIcon = "DefaultNetwork.png";
      else if (pItem->IsISO9660())
      strIcon = "DefaultDVDRom.png";
      else if (pItem->IsDVD())
      strIcon = "DefaultDVDRom.png";
      else if (pItem->IsCDDA())
      strIcon = "DefaultCDDA.png";
      else
      strIcon = "DefaultHardDisk.png";
    }

    pItem->SetIconImage(strIcon);
    if (share.m_iHasLock == 2 && g_settings.m_vecProfiles[0].getLockMode() != LOCK_MODE_EVERYONE)
      pItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_LOCKED);
    else
      pItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_NONE);

    items.Add(pItem);
  }

  return true;
}

/*!
 \brief Is the share \e strPath in the virtual directory.
 \param strPath Share to test
 \return Returns \e true, if share is in the virtual directory.
 \note The parameter \e strPath can not be a share with directory. Eg. "iso9660://dir" will return \e false.
    It must be "iso9660://".
 */
bool CVirtualDirectory::IsSource(const CStdString& strPath) const
{
  CStdString strPathCpy = strPath;
  strPathCpy.TrimRight("/");
  strPathCpy.TrimRight("\\");

  // just to make sure there's no mixed slashing in share/default defines
  // ie. f:/video and f:\video was not be recognised as the same directory,
  // resulting in navigation to a lower directory then the share.
  if(CUtil::IsDOSPath(strPathCpy))
  strPathCpy.Replace("/", "\\");

  VECSOURCES shares;
  GetSources(shares);
  for (int i = 0; i < (int)shares.size(); ++i)
  {
    const CMediaSource& share = shares.at(i);
    CStdString strShare = share.strPath;
    strShare.TrimRight("/");
    strShare.TrimRight("\\");
    if(CUtil::IsDOSPath(strShare))
    strShare.Replace("/", "\\");
    if (strShare == strPathCpy) return true;
  }
  return false;
}

/*!
 \brief Is the share \e path in the virtual directory.
 \param path Share to test
 \return Returns \e true, if share is in the virtual directory.
 \note The parameter \e path CAN be a share with directory. Eg. "iso9660://dir" will
       return the same as "iso9660://".
 */
bool CVirtualDirectory::IsInSource(const CStdString &path) const
{
  bool isSourceName;
  VECSOURCES shares;
  GetSources(shares);
  int iShare = CUtil::GetMatchingSource(path, shares, isSourceName);
  if (CUtil::IsOnDVD(path))
  { // check to see if our share path is still available and of the same type, as it changes during autodetect
    // and GetMatchingSource() is too naive at it's matching
    for (unsigned int i = 0; i < shares.size(); i++)
    {
      CMediaSource &share = shares[i];
      if (CUtil::IsOnDVD(share.strPath) && share.strPath.Equals(path.Left(share.strPath.GetLength())))
        return true;
    }
    return false;
  }
  // TODO: May need to handle other special cases that GetMatchingSource() fails on
  return (iShare > -1);
}

void CVirtualDirectory::GetSources(VECSOURCES &shares) const
{
  shares = *m_vecSources;
  // add our plug n play shares

  if (m_allowNonLocalSources)
  {
    g_mediaManager.GetRemovableDrives(shares);

    CUtil::AutoDetectionGetSource(shares);
  }

#ifdef HAS_DVD_DRIVE
  // and update our dvd share
  for (unsigned int i = 0; i < shares.size(); ++i)
  {
    CMediaSource& share = shares[i];
    if (share.m_iDriveType == CMediaSource::SOURCE_TYPE_DVD)
    {
      if(g_mediaManager.IsAudio(share.strPath))
      {
          share.strStatus = "Audio-CD";
          share.strPath = "cdda://local/";
        }
        else
        share.strStatus = g_mediaManager.GetDiskLabel(share.strPath);
      }
    }
#endif
  }
}

