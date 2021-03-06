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
#include "PluginDirectory.h"
#include "Util.h"
#ifdef HAS_PYTHON
#include "lib/libPython/XBPython.h"
#endif
#include "../utils/SingleLock.h"
#include "PluginSettings.h"
#include "GUIWindowManager.h"
#include "GUIDialogProgress.h"
#include "FileSystem/File.h"
#include "GUISettings.h"
#include "FileItem.h"
#include "LocalizeStrings.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

using namespace DIRECTORY;
using namespace std;

vector<CPluginDirectory *> CPluginDirectory::globalHandles;
CCriticalSection CPluginDirectory::m_handleLock;

CPluginDirectory::CPluginDirectory(void)
{
  m_fetchComplete = CreateEvent(NULL, false, false, NULL);
  m_listItems = new CFileItemList;
  m_fileResult = new CFileItem;
}

CPluginDirectory::~CPluginDirectory(void)
{
  CloseHandle(m_fetchComplete);
  delete m_listItems;
  delete m_fileResult;
}

int CPluginDirectory::getNewHandle(CPluginDirectory *cp)
{
  CSingleLock lock(m_handleLock);
  int handle = (int)globalHandles.size();
  globalHandles.push_back(cp);
  return handle;
}

void CPluginDirectory::removeHandle(int handle)
{
  CSingleLock lock(m_handleLock);
  if (handle >= 0 && handle < (int)globalHandles.size())
    globalHandles.erase(globalHandles.begin() + handle);
}

bool CPluginDirectory::StartScript(const CStdString& strPath)
{
  CURI url(strPath);

  CStdString fileName;

  // path is special://home/plugins/<path from here>
  CStdString pathToScript = "special://home/plugins/";
  CUtil::AddFileToFolder(pathToScript, url.GetHostName(), pathToScript);
  CUtil::AddFileToFolder(pathToScript, url.GetFileName(), pathToScript);
  CUtil::AddFileToFolder(pathToScript, "default.py", pathToScript);

  // base path
  CStdString basePath = "plugin://";
  CUtil::AddFileToFolder(basePath, url.GetHostName(), basePath);
  CUtil::AddFileToFolder(basePath, url.GetFileName(), basePath);

  // options
  CStdString options = url.GetOptions();
  CUtil::RemoveSlashAtEnd(options); // This MAY kill some scripts (eg though with a URL ending with a slash), but
                                    // is needed for all others, as XBMC adds slashes to "folders"

  // Load the plugin settings
  CLog::Log(LOGDEBUG, "%s - URL for plugin settings: %s", __FUNCTION__, url.GetFileName().c_str() );
  g_currentPluginSettings.Load(url);

  // Load language strings
  LoadPluginStrings(url);

  // reset our wait event, and grab a new handle
  ResetEvent(m_fetchComplete);
  int handle = getNewHandle(this);

  // clear out our status variables
  m_fileResult->Reset();
  m_listItems->Clear();
  m_listItems->m_strPath = strPath;
  m_cancelled = false;
  m_success = false;
  m_totalItems = 0;

  // setup our parameters to send the script
  CStdString strHandle;
  strHandle.Format("%i", handle);
  const char *plugin_argv[] = {basePath.c_str(), strHandle.c_str(), options.c_str(), NULL };

  // run the script
  CLog::Log(LOGDEBUG, "%s - calling plugin %s('%s','%s','%s')", __FUNCTION__, pathToScript.c_str(), plugin_argv[0], plugin_argv[1], plugin_argv[2]);
  bool success = false;
#ifdef HAS_PYTHON
  if (g_pythonParser.evalFile(pathToScript.c_str(), 3, (const char**)plugin_argv) >= 0)
  { // wait for our script to finish
    CStdString scriptName = url.GetFileName();
    CUtil::RemoveSlashAtEnd(scriptName);
    success = WaitOnScriptResult(pathToScript, scriptName);
  }
  else
#endif
    CLog::Log(LOGERROR, "Unable to run plugin %s", pathToScript.c_str());

  // free our handle
  removeHandle(handle);

  return success;
}

bool CPluginDirectory::GetPluginResult(const CStdString& strPath, CFileItem &resultItem)
{
  CPluginDirectory* newDir = new CPluginDirectory();

  bool success = newDir->StartScript(strPath);

  resultItem = *newDir->m_fileResult;

  delete newDir;

  return success;
}

bool CPluginDirectory::AddItem(int handle, const CFileItem *item, int totalItems)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, " %s - called with an invalid handle.", __FUNCTION__);
    return false;
  }

  CPluginDirectory *dir = globalHandles[handle];
  CFileItemPtr pItem(new CFileItem(*item));
  pItem->SetProperty("IsPlugin","1");
  dir->m_listItems->Add(pItem);
  dir->m_totalItems = totalItems;

  return !dir->m_cancelled;
}

bool CPluginDirectory::AddItems(int handle, const CFileItemList *items, int totalItems)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, " %s - called with an invalid handle.", __FUNCTION__);
    return false;
  }

  CPluginDirectory *dir = globalHandles[handle];
  CFileItemList pItemList = *items;
  dir->m_listItems->Append(pItemList);
  dir->m_totalItems = totalItems;

  return !dir->m_cancelled;
}

void CPluginDirectory::EndOfDirectory(int handle, bool success, bool replaceListing, bool cacheToDisc)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, " %s - called with an invalid handle.", __FUNCTION__);
    return;
  }
  CPluginDirectory *dir = globalHandles[handle];

  // set cache to disc
  dir->m_listItems->SetCacheToDisc(cacheToDisc ? CFileItemList::CACHE_IF_SLOW : CFileItemList::CACHE_NEVER);

  dir->m_success = success;
  dir->m_listItems->SetReplaceListing(replaceListing);

  if (!dir->m_listItems->HasSortDetails())
    dir->m_listItems->AddSortMethod(SORT_METHOD_NONE, 552, LABEL_MASKS("%L", "%D"));

  // Unload temporary language strings
  ClearPluginStrings();

  // set the event to mark that we're done
  SetEvent(dir->m_fetchComplete);
}

void CPluginDirectory::AddSortMethod(int handle, SORT_METHOD sortMethod)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, "%s - called with an invalid handle.", __FUNCTION__);
    return;
  }

  CPluginDirectory *dir = globalHandles[handle];

  // TODO: Add all sort methods and fix which labels go on the right or left
  switch(sortMethod)
  {
    case SORT_METHOD_LABEL:
    case SORT_METHOD_LABEL_IGNORE_THE:
      {
        if (g_guiSettings.GetBool("filelists.ignorethewhensorting"))
          dir->m_listItems->AddSortMethod(SORT_METHOD_LABEL_IGNORE_THE, 551, LABEL_MASKS("%T", "%D"));
        else
          dir->m_listItems->AddSortMethod(SORT_METHOD_LABEL, 551, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_TITLE:
    case SORT_METHOD_TITLE_IGNORE_THE:
      {
        if (g_guiSettings.GetBool("filelists.ignorethewhensorting"))
          dir->m_listItems->AddSortMethod(SORT_METHOD_TITLE_IGNORE_THE, 556, LABEL_MASKS("%T", "%D"));
        else
          dir->m_listItems->AddSortMethod(SORT_METHOD_TITLE, 556, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_ARTIST:
    case SORT_METHOD_ARTIST_IGNORE_THE:
      {
        if (g_guiSettings.GetBool("filelists.ignorethewhensorting"))
          dir->m_listItems->AddSortMethod(SORT_METHOD_ARTIST_IGNORE_THE, 557, LABEL_MASKS("%T", "%A"));
        else
          dir->m_listItems->AddSortMethod(SORT_METHOD_ARTIST, 557, LABEL_MASKS("%T", "%A"));
        break;
      }
    case SORT_METHOD_ALBUM:
    case SORT_METHOD_ALBUM_IGNORE_THE:
      {
        if (g_guiSettings.GetBool("filelists.ignorethewhensorting"))
          dir->m_listItems->AddSortMethod(SORT_METHOD_ALBUM_IGNORE_THE, 558, LABEL_MASKS("%T", "%B"));
        else
          dir->m_listItems->AddSortMethod(SORT_METHOD_ALBUM, 558, LABEL_MASKS("%T", "%B"));
        break;
      }
    case SORT_METHOD_DATE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_DATE, 552, LABEL_MASKS("%T", "%J"));
        break;
      }
    case SORT_METHOD_SIZE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_SIZE, 553, LABEL_MASKS("%T", "%I"));
        break;
      }
    case SORT_METHOD_FILE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_FILE, 561, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_TRACKNUM:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_TRACKNUM, 554, LABEL_MASKS("[%N. ]%T", "%D"));
        break;
      }
    case SORT_METHOD_DURATION:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_DURATION, 555, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_VIDEO_RATING:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_VIDEO_RATING, 563, LABEL_MASKS("%T", "%R"));
        break;
      }
    case SORT_METHOD_YEAR:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_YEAR, 562, LABEL_MASKS("%T", "%Y"));
        break;
      }
    case SORT_METHOD_SONG_RATING:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_SONG_RATING, 563, LABEL_MASKS("%T", "%R"));
        break;
      }
    case SORT_METHOD_GENRE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_GENRE, 515, LABEL_MASKS("%T", "%G"));
        break;
      }
    case SORT_METHOD_VIDEO_TITLE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_VIDEO_TITLE, 369, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_MPAA_RATING:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_MPAA_RATING, 563, LABEL_MASKS("%T", "%O"));
        break;
      }
    case SORT_METHOD_VIDEO_RUNTIME:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_VIDEO_RUNTIME, 555, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_STUDIO:
    case SORT_METHOD_STUDIO_IGNORE_THE:
      {
        if (g_guiSettings.GetBool("filelists.ignorethewhensorting"))
          dir->m_listItems->AddSortMethod(SORT_METHOD_STUDIO_IGNORE_THE, 572, LABEL_MASKS("%T", "%U"));
        else
          dir->m_listItems->AddSortMethod(SORT_METHOD_STUDIO, 572, LABEL_MASKS("%T", "%U"));
        break;
      }
    case SORT_METHOD_PROGRAM_COUNT:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_PROGRAM_COUNT, 567, LABEL_MASKS("%T", "%C"));
        break;
      }
    case SORT_METHOD_UNSORTED:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_UNSORTED, 571, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_NONE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_NONE, 552, LABEL_MASKS("%T", "%D"));
        break;
      }
    case SORT_METHOD_DRIVE_TYPE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_DRIVE_TYPE, 564, LABEL_MASKS()); // Preformatted
        break;
      }
    case SORT_METHOD_PLAYLIST_ORDER:
      {
        CStdString strTrackLeft=g_guiSettings.GetString("musicfiles.trackformat");
        CStdString strTrackRight=g_guiSettings.GetString("musicfiles.trackformatright");

        dir->m_listItems->AddSortMethod(SORT_METHOD_PLAYLIST_ORDER, 559, LABEL_MASKS(strTrackLeft, strTrackRight));
        break;
      }
    case SORT_METHOD_EPISODE:
      {
        dir->m_listItems->AddSortMethod(SORT_METHOD_EPISODE,20359,LABEL_MASKS("%E. %T","%R"));
        break;
      }
    case SORT_METHOD_PRODUCTIONCODE:
      {
        //dir->m_listItems.AddSortMethod(SORT_METHOD_PRODUCTIONCODE,20368,LABEL_MASKS("%E. %T","%P", "%E. %T","%P"));
        dir->m_listItems->AddSortMethod(SORT_METHOD_PRODUCTIONCODE,20368,LABEL_MASKS("%H. %T","%P", "%H. %T","%P"));
        break;
      }
    default:
      break;
  }
}

bool CPluginDirectory::GetDirectory(const CStdString& strPath, CFileItemList& items)
{ 
#ifndef HAS_PYTHON
  return false;
#endif

  CURI url(strPath);
  if (url.GetFileName().IsEmpty())
  { // called with no script - should never happen
    CLog::Log(LOGERROR, " called with no script - should NEVER happen, path = %s", strPath.c_str());
    return GetPluginsDirectory(url.GetHostName(), items);
  }
  
  CStdString fileName;
  CUtil::AddFileToFolder(url.GetFileName(), "default.py", fileName);
  
  // path is Q:\plugins\<path from here>
  CStdString pathToScript = GetPathToPlugin(url.GetHostName(), fileName);
  
  // base path
  CStdString basePath = "plugin://";
  CUtil::AddFileToFolder(basePath, url.GetHostName(), basePath);
  CUtil::AddFileToFolder(basePath, url.GetFileName(), basePath);
  
  // options
  CStdString options = url.GetOptions();
  CUtil::RemoveSlashAtEnd(options); // This MAY kill some scripts (eg though with a URL ending with a slash), but
  // is needed for all others, as XBMC adds slashes to "folders"
  
  // Load the plugin settings
  CLog::Log(LOGDEBUG, "%s - URL for plugin settings: %s", __FUNCTION__, url.GetFileName().c_str() );
  g_currentPluginSettings.Load(url);
  
  // Load language strings
  LoadPluginStrings(url);
  
  // reset our wait event, and grab a new handle
  ResetEvent(m_fetchComplete);
  int handle = getNewHandle(this);
  
  // clear out our status variables
  m_listItems->Clear();
  m_listItems->m_strPath = strPath;
  m_cancelled = false;
  m_success = false;
  m_totalItems = 0;
  
  // setup our parameters to send the script
  CStdString strHandle;
  strHandle.Format("%i", handle);
  const char *plugin_argv[] = {basePath.c_str(), strHandle.c_str(), options.c_str(), NULL };

  // run the script
  CLog::Log(LOGDEBUG, "%s - calling plugin %s('%s','%s','%s')", __FUNCTION__, pathToScript.c_str(), plugin_argv[0], plugin_argv[1], plugin_argv[2]);
  bool success = false;
#ifdef HAS_PYTHON
  if (g_pythonParser.evalFile(pathToScript.c_str(), 3, (const char**)plugin_argv) >= 0)
  { 
    // wait for our script to finish
    CStdString scriptName = url.GetFileName();
    CUtil::RemoveSlashAtEnd(scriptName);

    success = WaitOnScriptResult(pathToScript, scriptName);
  }
  else
#endif
  {
    CLog::Log(LOGERROR, "Unable to run plugin %s", pathToScript.c_str());
  }
  
  // free our handle
  removeHandle(handle);
  
  if (success) 
  {
    if(m_listItems->Size() == 0)
    {
      // The script returned (success=TRUE) and (NumOfItems=0), which means that 
      // the user exit the keyboard with ESC or search for an empty string.
      // We will set the retVal (success) to FALSE but set the property "NotErrorDontShowErrorDialog" to TRUE
      // because we don't want to popup an error dialog
      
      success = false;
      
      items.SetProperty("NotErrorDontShowErrorDialog", true);
 
      CLog::Log(LOGERROR,"CPluginDirectory::GetDirectory - Set the property NotErrorDontShowErrorDialog to TRUE (search)");
    }
    else
    {
      // append the items to the list
      items.Assign(*m_listItems, true); // true to keep the current items
      }      
    
    m_listItems->Clear();
  }

  return success;
}

bool CPluginDirectory::RunScriptWithParams(const CStdString& strPath)
{
  CURI url(strPath);
  if (url.GetFileName().IsEmpty()) // called with no script - should never happen
    return false;

  // Load the settings incase they changed while in the plugins directory
  g_currentPluginSettings.Load(url);

  // Load language strings
  LoadPluginStrings(url);

  CStdString fileName;
  CUtil::AddFileToFolder(url.GetFileName(), "default.py", fileName);

  // path is special://home/plugins/<path from here>
  CStdString pathToScript = GetPathToPlugin(url.GetHostName(), fileName);
  
  // options
  CStdString options = url.GetOptions();
  CUtil::RemoveSlashAtEnd(options); // This MAY kill some scripts (eg though with a URL ending with a slash), but
                                    // is needed for all others, as XBMC adds slashes to "folders"
  // base path
  CStdString basePath = "plugin://";
  CUtil::AddFileToFolder(basePath, url.GetHostName(), basePath);
  CUtil::AddFileToFolder(basePath, url.GetFileName(), basePath);

  // setup our parameters to send the script
  CStdString strHandle;
  strHandle.Format("%i", -1);
  const char *argv[3];
  argv[0] = basePath.c_str();
  argv[1] = strHandle.c_str();
  argv[2] = options.c_str();

  // run the script
#ifdef HAS_PYTHON
  CLog::Log(LOGDEBUG, "%s - calling plugin %s('%s','%s','%s')", __FUNCTION__, pathToScript.c_str(), argv[0], argv[1], argv[2]);
  if (g_pythonParser.evalFile(pathToScript.c_str(), 3, (const char**)argv) >= 0)
    return true;
  else
#endif
    CLog::Log(LOGERROR, "Unable to run plugin %s", pathToScript.c_str());

  return false;
}

bool CPluginDirectory::HasPlugins(const CStdString &type)
{
  CStdString path = "special://home/plugins/";
  CUtil::AddFileToFolder(path, type, path);
  CFileItemList items;
  if (CDirectory::GetDirectory(path, items, "/", false))
  {
    for (int i = 0; i < items.Size(); i++)
    {
      CFileItemPtr item = items[i];
      if (item->m_bIsFolder && !item->IsParentFolder() && !item->m_bIsShareOrDrive)
      {
        CStdString defaultPY;
        CUtil::AddFileToFolder(item->m_strPath, "default.py", defaultPY);
        if (XFILE::CFile::Exists(defaultPY))
          return true;
      }
    }
  }
  return false;
}

bool CPluginDirectory::GetPluginsDirectory(const CStdString &type, CFileItemList &items)
{
  // retrieve our folder
  CStdString pluginsFolder = "special://home/plugins";
  CUtil::AddFileToFolder(pluginsFolder, type, pluginsFolder);
  CUtil::AddSlashAtEnd(pluginsFolder);

  if (!CDirectory::GetDirectory(pluginsFolder, items, "*.py", false))
    return false;

  items.m_strPath.Replace("special://home/plugins/", "plugin://");

  // flatten any folders - TODO: Assigning of thumbs
  for (int i = 0; i < items.Size(); i++)
  {
    CFileItemPtr item = items[i];
    CStdString strThumb;
    CUtil::AddFileToFolder(item->m_strPath,"default.tbn",strThumb);
    if (XFILE::CFile::Exists(strThumb))
      item->SetThumbnailImage(strThumb);
    item->m_strPath.Replace("special://home/plugins/", "plugin://");
    item->m_strPath.Replace("\\", "/");
  }
  return true;
}

bool CPluginDirectory::WaitOnScriptResult(const CStdString &scriptPath, const CStdString &scriptName)
{
  const unsigned int timeBeforeProgressBar = 0;
  const unsigned int timeToKillScript = 500;

  unsigned int startTime = CTimeUtils::GetTimeMS();
  CGUIDialogProgress *progressBar = NULL;
  
  CLog::Log(LOGDEBUG, "%s - waiting on the %s plugin...", __FUNCTION__, scriptName.c_str());
  while (true)
  {
    CSingleExit ex(g_graphicsContext);
    // check if the python script is finished
    if (WaitForSingleObject(m_fetchComplete, 20) == WAIT_OBJECT_0)
    { 
      // python has returned
      CLog::Log(LOGDEBUG, "%s- plugin returned %s", __FUNCTION__, m_success ? "successfully" : "failure");
      break;
    }
    ex.Restore();

    // check our script is still running
#ifdef HAS_PYTHON
    int id = g_pythonParser.getScriptId(scriptPath.c_str());
    if (id == -1)
#endif
    { // nope - bail
      CLog::Log(LOGDEBUG, " %s - plugin exited prematurely - terminating", __FUNCTION__);
      m_success = false;
      break;
    }

    // check whether we should pop up the progress dialog
    if (!progressBar && CTimeUtils::GetTimeMS() - startTime > timeBeforeProgressBar)
    { // loading takes more then 1.5 secs, show a progress dialog
      progressBar = (CGUIDialogProgress *)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
      if (progressBar)
      {
        progressBar->SetHeading(scriptName);
        progressBar->SetLine(0, 1040);
        progressBar->SetLine(1, "");
        progressBar->SetLine(2, "");
        progressBar->StartModal();
      }
    }

    if (progressBar)
    { // update the progress bar and check for user cancel
      CStdString label;
      if (m_totalItems > 0)
      {
        label.Format(g_localizeStrings.Get(1042).c_str(), m_listItems->Size(), m_totalItems);
        progressBar->SetPercentage((int)((m_listItems->Size() * 100 ) / m_totalItems));
        progressBar->ShowProgressBar(true);
      }
      else
        label.Format(g_localizeStrings.Get(1041).c_str(), m_listItems->Size());
      progressBar->SetLine(2, label);
      progressBar->Progress();
      if (progressBar->IsCanceled())
      { // user has cancelled our process - cancel our process
        if (!m_cancelled)
        {
          m_cancelled = true;
          startTime = CTimeUtils::GetTimeMS();
        }
        if (m_cancelled && CTimeUtils::GetTimeMS() - startTime > timeToKillScript)
        { // cancel our script
#ifdef HAS_PYTHON
          int id = g_pythonParser.getScriptId(scriptPath.c_str());
          if (id != -1 && g_pythonParser.isRunning(id))
          {
            CLog::Log(LOGDEBUG, "%s- cancelling plugin %s", __FUNCTION__, scriptName.c_str());
            g_pythonParser.stopScript(id);
            break;
          }
#endif
        }
      }
    }
  }
  if (progressBar)
    progressBar->Close();

  return !m_cancelled && m_success;
}

void CPluginDirectory::SetResolvedUrl(int handle, bool success, const CFileItem *resultItem)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, " %s - called with an invalid handle.", __FUNCTION__);
    return;
  }
  CPluginDirectory* dir  = globalHandles[handle];

  dir->m_success = success;
  *dir->m_fileResult = *resultItem;

  // set the event to mark that we're done
  SetEvent(dir->m_fetchComplete);
}

void CPluginDirectory::SetContent(int handle, const CStdString &strContent)
{
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, "%s called with an invalid handle.", __FUNCTION__);
    return;
  }

  CPluginDirectory *dir = globalHandles[handle];
  dir->m_listItems->SetContent(strContent);
}

void CPluginDirectory::SetProperty(int handle, const CStdString &strProperty, const CStdString &strValue)
{
  CSingleLock lock(m_handleLock);
  if (handle < 0 || handle >= (int)globalHandles.size())
  {
    CLog::Log(LOGERROR, "%s called with an invalid handle.", __FUNCTION__);
    return;
  }

  CPluginDirectory *dir = globalHandles[handle];
  dir->m_listItems->SetProperty(strProperty, strValue);
}

void CPluginDirectory::LoadPluginStrings(const CURI &url)
{
  CStdString pathToPlugin = GetPathToPlugin(url.GetHostName(), url.GetFileName());
  
  // Path where the language strings reside
  CStdString pathToLanguageFile = pathToPlugin;
  CStdString pathToFallbackLanguageFile = pathToPlugin;
  CUtil::AddFileToFolder(pathToLanguageFile, "resources", pathToLanguageFile);
  CUtil::AddFileToFolder(pathToFallbackLanguageFile, "resources", pathToFallbackLanguageFile);
  CUtil::AddFileToFolder(pathToLanguageFile, "language", pathToLanguageFile);
  CUtil::AddFileToFolder(pathToFallbackLanguageFile, "language", pathToFallbackLanguageFile);
  CUtil::AddFileToFolder(pathToLanguageFile, g_guiSettings.GetString("locale.language"), pathToLanguageFile);
  CUtil::AddFileToFolder(pathToFallbackLanguageFile, "english", pathToFallbackLanguageFile);
  CUtil::AddFileToFolder(pathToLanguageFile, "strings.xml", pathToLanguageFile);
  CUtil::AddFileToFolder(pathToFallbackLanguageFile, "strings.xml", pathToFallbackLanguageFile);

  // Load language strings temporarily
  g_localizeStringsTemp.Load(pathToLanguageFile, pathToFallbackLanguageFile);
}

void CPluginDirectory::ClearPluginStrings()
{
  // Unload temporary language strings
  g_localizeStringsTemp.Clear();
}

CStdString CPluginDirectory::GetPathToPlugin(const CStdString& hostname, const CStdString& filename)
{
  // First try apps
  CStdString pathToPlugin = "special://home/apps/";
  CUtil::AddFileToFolder(pathToPlugin, filename, pathToPlugin);
  if (XFILE::CFile::Exists(pathToPlugin))
  {
    return pathToPlugin;
  }
  return StringUtils::EmptyString;
}

