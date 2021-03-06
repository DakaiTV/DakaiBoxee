/*
* XBMC Media Center
* Copyright (c) 2002 Frodo
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

#include "Thread.h"
#ifndef _LINUX
#include <process.h>
#include "win32exception.h"
#ifndef _MT
#pragma message( "Please compile using multithreaded run-time libraries" )
#endif
typedef unsigned (WINAPI *PBEGINTHREADEX_THREADFUNC)(LPVOID lpThreadParameter);
#else
#include "PlatformInclude.h"
#include "XHandle.h"
#include <signal.h>
typedef int (*PBEGINTHREADEX_THREADFUNC)(LPVOID lpThreadParameter);
#endif

#include "log.h"
#include "GraphicContext.h"
#include "utils/TimeUtils.h"

#ifdef USE_MINI_DUMPS
#include "MiniDumps.h"
#endif

#include "SingleLock.h"
#include "GUIDialogProgress.h"
#include "Application.h"
#include "GUIWindowManager.h"

#ifdef __APPLE__
//
// Use pthread's built-in support for TLS, it's more portable.
//
static pthread_once_t keyOnce = PTHREAD_ONCE_INIT;
static pthread_key_t  tlsLocalThread = 0;

//
// Called once and only once.
//
static void MakeTlsKeys()
{
  pthread_key_create(&tlsLocalThread, NULL);
}

#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


#define MS_VC_EXCEPTION 0x406d1388
typedef struct tagTHREADNAME_INFO 
{ 
  DWORD dwType; // must be 0x1000 
  LPCSTR szName; // pointer to name (in same addr space) 
  DWORD dwThreadID; // thread ID (-1 caller thread) 
  DWORD dwFlags; // reserved for future use, most be zero 
} THREADNAME_INFO;

CThread::CThread()
{
#ifdef __APPLE__
  // Initialize thread local storage and local thread pointer.
  pthread_once(&keyOnce, MakeTlsKeys);
#endif

  m_bStop = false;
  m_bDone = false;

  m_bAutoDelete = false;
  m_ThreadHandle = NULL;
  m_ThreadId = 0;
  m_iLastTime = 0;
  m_iLastUsage = 0;
  m_fLastUsage = 0.0f;
  m_StopEvent = CreateEvent(NULL, TRUE, TRUE, NULL);

  m_pRunnable=NULL;
}

CThread::CThread(IRunnable* pRunnable)
{  
  m_bStop = false;
  m_bDone = false;

  m_bAutoDelete = false;
  m_ThreadHandle = NULL;
  m_ThreadId = 0;
  m_iLastTime = 0;
  m_iLastUsage = 0;
  m_fLastUsage = 0.0f;
  m_StopEvent = CreateEvent(NULL, TRUE, TRUE, NULL);

  m_pRunnable=pRunnable;
  m_pRunnable->pParent = this;
}

CThread::~CThread()
{
  if (m_ThreadHandle != NULL)
  {
    CloseHandle(m_ThreadHandle);
  }
  m_ThreadHandle = NULL;

  if (m_StopEvent)
    CloseHandle(m_StopEvent);
}

#ifdef _LINUX
#ifdef __APPLE__
// Use pthread-based TLS.
#define LOCAL_THREAD ((CThread* )pthread_getspecific(tlsLocalThread))
#else
// Use compiler-based TLS.
__thread CThread* pLocalThread = NULL;
#define LOCAL_THREAD pLocalThread
#endif
void CThread::term_handler (int signum)
{
  CLog::Log(LOGERROR,"thread 0x%p got signal %d. calling OnException and terminating thread abnormally.", (void*) pthread_self(), signum);
  if (LOCAL_THREAD)
  {
    LOCAL_THREAD->m_bStop = TRUE;
    if (LOCAL_THREAD->m_StopEvent)
      SetEvent(LOCAL_THREAD->m_StopEvent);

    LOCAL_THREAD->OnException();
    if( LOCAL_THREAD->IsAutoDelete() )
      delete LOCAL_THREAD;
  }

  pthread_exit(NULL);
}
int CThread::staticThread(void* data)
#else
DWORD WINAPI CThread::staticThread(LPVOID* data)
#endif
{
  CThread* pThread = (CThread*)(data);
  if (!pThread) {
    CLog::Log(LOGERROR,"%s, sanity failed. thread is NULL.",__FUNCTION__);
    return 1;
  }

  CLog::Log(LOGDEBUG,"thread start, auto delete: %d",pThread->IsAutoDelete());
#ifdef USE_MINI_DUMPS
  g_MiniDumps.Install();
#endif

#ifndef _LINUX
  /* install win32 exception translator */
  win32_exception::install_handler();
#else
#ifndef __APPLE__
  pLocalThread = pThread;
#endif
  struct sigaction action;
  action.sa_handler = term_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  //sigaction (SIGABRT, &action, NULL);
  //sigaction (SIGSEGV, &action, NULL);
#endif
 
#ifdef __APPLE__
  // Set the TLS.
  pthread_setspecific(tlsLocalThread, (void*)pThread);
#endif

  try 
  {
    pThread->OnStartup();
  }
#ifndef _LINUX
  catch (const win32_exception &e) 
  {
    e.writelog(__FUNCTION__);
    if( pThread->IsAutoDelete() )
    {
      delete pThread;
      _endthreadex(123);
      return 0;
    }
  }
#endif
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Unhandled exception caught in thread startup, aborting. auto delete: %d", __FUNCTION__, pThread->IsAutoDelete());
    if( pThread->IsAutoDelete() )
    {
      delete pThread;
#ifndef _LINUX
      _endthreadex(123);
#endif
      return 0;
    }
  }

  try
  {
    pThread->Process();
  }
#ifndef _LINUX
  catch (const access_violation &e) 
  {
    e.writelog(__FUNCTION__);
  }
  catch (const win32_exception &e) 
  {
    e.writelog(__FUNCTION__);
  }
#endif
  catch(std::exception& e)
  {
    CLog::Log(LOGERROR, "%s - Unhandled exception caught in thread process, attemping cleanup in OnExit: %s", __FUNCTION__, e.what());
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Unhandled exception caught in thread process, attemping cleanup in OnExit", __FUNCTION__); 
  }

  try
  {
    pThread->OnExit();
  }
#ifndef _LINUX
  catch (const access_violation &e) 
  {
    e.writelog(__FUNCTION__);
  }
  catch (const win32_exception &e) 
  {
    e.writelog(__FUNCTION__);
  }
#endif
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Unhandled exception caught in thread exit", __FUNCTION__); 
  }

  ::EnterCriticalSection(pThread->m_lock);
  pThread->m_bStop = true;
  if (pThread->m_ThreadHandle)
  {
    SetEvent(pThread->m_ThreadHandle); 
#ifdef _LINUX
    pThread->m_ThreadHandle->m_threadValid = false;
#endif
    CloseHandle(pThread->m_ThreadHandle);
    pThread->m_ThreadHandle = NULL;
  }
  
  pThread->m_bDone = true;
  
  if ( pThread->IsAutoDelete() )
  {
    ::LeaveCriticalSection(pThread->m_lock);

    CLog::Log(LOGDEBUG,"Thread %"PRIu64" terminating (autodelete)", (uint64_t)CThread::GetCurrentThreadId());

    if (pThread->m_pRunnable) {

      delete pThread->m_pRunnable;
    }

    delete pThread;
    pThread = NULL;
  }
  else
  {
    ::LeaveCriticalSection(pThread->m_lock);
    CLog::Log(LOGDEBUG,"Thread %"PRIu64" terminating", (uint64_t)CThread::GetCurrentThreadId());  
  }

#ifndef _LINUX
  _endthreadex(123);
#endif
  return 0;
}

void CThread::Create(bool bAutoDelete, unsigned stacksize)
{
  if (m_ThreadHandle != NULL)
  {
    throw 1; //ERROR should not b possible!!!
  }
  m_iLastTime = CTimeUtils::GetTimeMS() * 10000;
  m_iLastUsage = 0;
  m_fLastUsage = 0.0f;
  m_bAutoDelete = bAutoDelete;
  m_bStop = false;
  ::ResetEvent(m_StopEvent);
 
  CSingleLock lock(m_lock);
  m_ThreadHandle = (HANDLE)_beginthreadex(NULL, stacksize, (PBEGINTHREADEX_THREADFUNC)staticThread, (void*)this, 0, &m_ThreadId);

  // special case that the thread had terminated before its handle is set - we need to mark it as "dead"
  if (m_ThreadHandle && m_bStop)
    SetEvent(m_ThreadHandle);
}

bool CThread::IsAutoDelete() const
{
  return m_bAutoDelete;
}

void CThread::SetAutoDelete(bool bAutoDelete)
{
  m_bAutoDelete = bAutoDelete;
}

void CThread::Detach(bool bAutoDelete)
{
  CSingleLock lock(m_lock);
  if (m_bAutoDelete)
  {
    CLog::Log(LOGERROR,"Detach called on an autodelete thread! cant determine state - returning - hopefully without a crash");
    return;
  }
    
  m_bAutoDelete = bAutoDelete;
  m_bStop = true;
  SetEvent(m_StopEvent);
  
  if (m_bDone && bAutoDelete)
    delete this;
}

void CThread::StopThread(bool bWait /*= true*/)
{
  CSingleLock lock(m_lock);
  
  m_bStop = true;
  SetEvent(m_StopEvent);
  if (m_ThreadHandle && bWait && !m_bAutoDelete)
  {
    lock.Leave();
    g_application.getApplicationMessenger().ReleaseThreadMessages(ThreadId());
    WaitForThreadExit(INFINITE);
  }  
}

ThreadIdentifier CThread::ThreadId() const
{
#ifdef _LINUX
  if (m_ThreadHandle && m_ThreadHandle->m_threadValid)
    return m_ThreadHandle->m_hThread;
#else
  return m_ThreadId;
#endif
  return 0;
}


CThread::operator HANDLE()
{
  return m_ThreadHandle;
}

CThread::operator HANDLE() const
{
  return m_ThreadHandle;
}

bool CThread::SetPriority(const int iPriority)
// Set thread priority
// Return true for success
{
  if (m_ThreadHandle)
  {
#ifndef _LINUX
    return ( SetThreadPriority( m_ThreadHandle, iPriority ) == TRUE );
#else
    return true;
#endif
  }
  else
  {
    return false;
  }
}

void CThread::SetName( LPCTSTR szThreadName )
{
  THREADNAME_INFO info; 
  info.dwType = 0x1000; 
  info.szName = szThreadName; 
  info.dwThreadID = m_ThreadId;
  info.dwFlags = 0; 
#ifndef _LINUX
  try 
  { 
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (DWORD *)&info); 
  } 
  catch(...)
  { 
  }  
#endif
}

bool CThread::WaitForThreadExit(unsigned int milliseconds)
// Waits for thread to exit, timeout in given number of msec.
// Returns true when thread ended
{
  CSingleLock lock(m_lock);
  if (!m_ThreadHandle) return true;

#ifndef _LINUX
  // boost priority of thread we are waiting on to same as caller
  int callee = GetThreadPriority(m_ThreadHandle);
  int caller = GetThreadPriority(GetCurrentThread());
  if(caller > callee)
    SetThreadPriority(m_ThreadHandle, caller);

  lock.Leave();
  if (::WaitForSingleObject(m_ThreadHandle, milliseconds) != WAIT_TIMEOUT)
    return true;
  lock.Enter();

  // restore thread priority if thread hasn't exited
  if(caller > callee && m_ThreadHandle)
    SetThreadPriority(m_ThreadHandle, callee);
#else
  HANDLE copy=NULL;
  DuplicateHandle(GetCurrentProcess(), m_ThreadHandle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);  
  lock.Leave();
  int nRet = ::WaitForSingleObject(m_ThreadHandle, milliseconds);
  lock.Enter(); // we have to re-enter the lock to make sure that all finalization code is done (its locked). 
                // otherwise - if we return from this function too soon - the caller can delete this object (before all finalization code is done).
  if (copy)
    CloseHandle(copy);
  
  if (nRet != WAIT_TIMEOUT)
    return true;
#endif

  return false;
}

HANDLE CThread::ThreadHandle()
{
  return m_ThreadHandle;
}

void CThread::Process()
{
  if(m_pRunnable)
    m_pRunnable->Run(); 
}

float CThread::GetRelativeUsage()
{
  unsigned __int64 iTime = CTimeUtils::GetTimeMS();
  iTime *= 10000; // convert into 100ns tics

  // only update every 1 second
  if( iTime < m_iLastTime + 1000*10000 ) return m_fLastUsage;

  FILETIME CreationTime, ExitTime, UserTime, KernelTime;
  if( GetThreadTimes( m_ThreadHandle, &CreationTime, &ExitTime, &KernelTime, &UserTime ) )
  {
    unsigned __int64 iUsage = 0;
    iUsage += (((unsigned __int64)UserTime.dwHighDateTime) << 32) + ((unsigned __int64)UserTime.dwLowDateTime);
    iUsage += (((unsigned __int64)KernelTime.dwHighDateTime) << 32) + ((unsigned __int64)KernelTime.dwLowDateTime);

    if(m_iLastUsage > 0 && m_iLastTime > 0)
    m_fLastUsage = (float)( iUsage - m_iLastUsage ) / (float)( iTime - m_iLastTime );
      
    m_iLastUsage = iUsage;
    m_iLastTime = iTime;

    return m_fLastUsage;
  }
  return 0.0f; 
}

bool CThread::IsCurrentThread() const
{
  return IsCurrentThread(ThreadId());
}


ThreadIdentifier CThread::GetCurrentThreadId()
{
#ifdef _LINUX
  return pthread_self();
#else
  return ::GetCurrentThreadId();
#endif
}

bool CThread::Equals(const ThreadIdentifier tid1, const ThreadIdentifier tid2)
{
#ifdef _LINUX
  return pthread_equal(tid1, tid2);
#else
  return (tid1 == tid2);
#endif
}

bool CThread::IsCurrentThread(const ThreadIdentifier tid)
{
#ifdef _LINUX
  return pthread_equal(pthread_self(), tid);
#else
  return (::GetCurrentThreadId() == tid);
#endif
}


DWORD CThread::WaitForSingleObject(HANDLE hHandle, unsigned int milliseconds) const
{
  if(milliseconds > 10 && IsCurrentThread())
  {
    HANDLE handles[2] = {hHandle, m_StopEvent};
    DWORD result = ::WaitForMultipleObjects(2, handles, false, milliseconds);

    if(result == WAIT_TIMEOUT || result == WAIT_OBJECT_0)
      return result;

    if( milliseconds == INFINITE )
      return WAIT_ABANDONED;
    else
      return WAIT_TIMEOUT;
  }
  else
    return ::WaitForSingleObject(hHandle, milliseconds);
}

DWORD CThread::WaitForMultipleObjects(DWORD nCount, HANDLE *lpHandles, BOOL bWaitAll, unsigned int milliseconds) const
{
  // for now not implemented
  return ::WaitForMultipleObjects(nCount, lpHandles, bWaitAll, milliseconds);
}

void CThread::Sleep(unsigned int milliseconds)
{
  if(milliseconds > 10 && IsCurrentThread())
    ::WaitForSingleObject(m_StopEvent, milliseconds);
  else
    ::Sleep(milliseconds);
}
