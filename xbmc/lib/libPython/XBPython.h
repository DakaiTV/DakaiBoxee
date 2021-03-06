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

#include "XBPyThread.h"
#include "cores/IPlayer.h"
#include "Application.h"
#include "XBPyPersistentThread.h"
#include "utils/CriticalSection.h"

#include <vector>

typedef struct {
  int id;
  bool bDone;
  std::string strFile;
  XBPyThread *pyThread;
}PyElem;

class LibraryLoader;

typedef std::vector<PyElem> PyList;
typedef std::vector<PVOID> PlayerCallbackList;
typedef std::vector<LibraryLoader*> PythonExtensionLibraries;

// BOXEE
class XBPythonAppContext
{
public:
  XBPyPersistentThread *m_pyThread;
};
// End BOXEE

class XBPython : public IPlayerCallback
{
public:
  XBPython();
  virtual ~XBPython();
  virtual void OnPlayBackEnded(bool bError = false, const CStdString& error = "");
  virtual void OnPlayBackStarted();
  virtual void OnPlayBackStopped();
  virtual void OnQueueNextItem() {};
  void	RegisterPythonPlayerCallBack(IPlayerCallback* pCallback);
  void	UnregisterPythonPlayerCallBack(IPlayerCallback* pCallback);
  void	Initialize();
  void	Finalize();
  void	FreeResources();
  void	Process();

  void PulseGlobalEvent();
  void WaitForEvent(HANDLE hEvent, unsigned int timeout);

  int	ScriptsSize();
  int	GetPythonScriptId(int scriptPosition);
  int   evalFile(const char *);
  int   evalFile(const char *, const unsigned int, const char **);
  int   evalString(const char *, const unsigned int argc = 0, const char ** argv = NULL);
  
// BOXEE
  int   evalFileInContext(const char *, const std::string& context, const CStdString& partnerId, const unsigned int argc = 0, const char ** argv = NULL);
  //int   evalStringInContext(const char *pythonCode, const std::string& path, const std::string& context, const CStdString& securityLevel, const unsigned int argc = 0, const char ** argv = NULL);
  int   evalStringInContext(const char *pythonCode, const std::string& path, const std::string& context, const CStdString& partnerId, const std::vector<CStdString>& params);
  XBPythonAppContext* GetContext(const std::string& _contextId, const CStdString& partnerId);
  void RemoveContext(const std::string& _contextId);
  void RemoveContextAll();
  bool HasPartnerThreadRunning(const CStdString& partnerId, const ThreadIdentifier& threadId);
  PyThreadState* CreateNewInterpreter();  
// End BOXEE
  
  bool	isRunning(int scriptId);
  bool  isStopping(int scriptId);
  void	setDone(int id);

  // inject xbmcstuff into the interpreter.
  // should be called for every new interpreter
  void InitializeInterpreter();
  
  // remove modules and references when interpreter done
  void DeInitializeInterpreter();

  void RegisterExtensionLib(LibraryLoader *pLib);
  void UnregisterExtensionLib(LibraryLoader *pLib);
  void UnloadExtensionLibs();

  //only should be called from thread which is running the script
  void  stopScript(int scriptId);

  // returns NULL if script doesn't exist or if script doesn't have a filename
  const char*	getFileName(int scriptId);
  
  // returns -1 if no scripts exist with specified fReilename
  int getScriptId(const char* strFile);

  PyThreadState *getMainThreadState();

  bool m_bStartup;
  bool m_bLogin;
private:
  bool              FileExist(const char* strFile);

  int               m_nextid;
  PyThreadState*    m_mainThreadState;
  ThreadIdentifier  m_ThreadId;
  bool              m_bInitialized;
  HANDLE            m_hEvent;
  int               m_iDllScriptCounter; // to keep track of the total scripts running that need the dll
  HMODULE           m_hModule;

  //Vector with list of threads used for running scripts
  PyList m_vecPyList;
  PlayerCallbackList m_vecPlayerCallbackList;
  CCriticalSection	m_critSection;
#ifdef USE_PYTHON_DLL
  LibraryLoader*    m_pDll;
#endif
  
  // any global events that scripts should be using
  HANDLE m_globalEvent;

  // in order to finalize and unload the python library, need to save all the extension libraries that are
  // loaded by it and unload them first (not done by finalize)
  PythonExtensionLibraries m_extensions;

// BOXEE
  std::map<std::string, XBPythonAppContext*> m_contextMap;
// End BOXEE    
};

extern XBPython g_pythonParser;
