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

#include "URL.h"
#include "utils/RegExp.h"
#include "utils/log.h"
#include "Util.h"
#include "FileSystem/File.h"
#include "FileItem.h"
#include "FileSystem/StackDirectory.h"
#ifndef _LINUX
#include <sys\types.h>
#include <sys\stat.h>
#endif

using namespace std;

CStdString URLEncodeInline(const CStdString& strData)
{
  CStdString buffer = strData;
  CUtil::URLEncode(buffer);
  return buffer;
}

CURI::CURI(const CStdString& strURL1)
{
  m_strHostName = "";
  m_strDomain = "";
  m_strUserName = "";
  m_strPassword = "";
  m_strShareName="";
  m_strFileName = "";
  m_strProtocol = "";
  m_strFileType = "";
  m_iPort = 0;

  // start by validating the path
  CStdString strURL = ValidatePath(strURL1);

  // strURL can be one of the following:
  // format 1: protocol://[username:password]@hostname[:port]/directoryandfile
  // format 2: protocol://file
  // format 3: drive:directoryandfile
  //
  // first need 2 check if this is a protocol or just a normal drive & path
  if (!strURL.size()) return ;
  if (strURL.Equals("?", true)) return;

  // form is format 1 or 2
  // format 1: protocol://[domain;][username:password]@hostname[:port]/directoryandfile
  // format 2: protocol://file

  // decode protocol
  int iPos = strURL.Find("://");
  if (iPos < 0)
  {
    // This is an ugly hack that needs some work.
    // example: filename /foo/bar.zip/alice.rar/bob.avi
    // This should turn into zip://rar:///foo/bar.zip/alice.rar/bob.avi
    iPos = 0;
    while (1)
    {
      iPos = strURL.Find(".zip/", iPos);
      int extLen = 3;
      if (iPos < 0)
      {
        /* set filename and update extension*/
        SetFileName(strURL);
        return ;
      }
      iPos += extLen + 1;
      CStdString archiveName = strURL.Left(iPos);
      struct __stat64 s;
      if (XFILE::CFile::Stat(archiveName, &s) == 0)
      {
#ifdef _LINUX
        if (!S_ISDIR(s.st_mode))
#else
        if (!(s.st_mode & S_IFDIR))
#endif
        {
          CUtil::URLEncode(archiveName);
          CURI c((CStdString)"zip" + "://" + archiveName + '/' + strURL.Right(strURL.size() - iPos - 1));
          *this = c;
          return;
        }
      }
    }
  }
  else
  {
    SetProtocol(strURL.Left(iPos));
    iPos += 3;
  }


  // virtual protocols
  // why not handle all format 2 (protocol://file) style urls here?
  // ones that come to mind are iso9660, cdda, musicdb, etc.
  // they are all local protocols and have no server part, port number, special options, etc.
  // this removes the need for special handling below.
  if (
    m_strProtocol.Equals("stack") ||
    m_strProtocol.Equals("virtualpath") ||
    m_strProtocol.Equals("multipath") ||
    m_strProtocol.Equals("filereader") ||
    m_strProtocol.Equals("special")
    )
  {
    SetFileName(strURL.Mid(iPos));
    return;
  }

  // check for username/password - should occur before first /
  if (iPos == -1) iPos = 0;

  // for protocols supporting options, chop that part off here
  // maybe we should invert this list instead?
  int iEnd = strURL.length();
  const char* sep = NULL;

  if(m_strProtocol.Equals("http")
    || m_strProtocol.Equals("https")
    || m_strProtocol.Equals("shout")
    || m_strProtocol.Equals("tuxbox")
    || m_strProtocol.Equals("daap")
    || m_strProtocol.Equals("plugin")
    || m_strProtocol.Equals("hdhomerun")
    || m_strProtocol.Equals("rtsp")
    || m_strProtocol.Equals("zip")
    || m_strProtocol.Equals("app"))
    sep = "?;#|";
  else if(m_strProtocol.Equals("ftp")
       || m_strProtocol.Equals("ftps")
       || m_strProtocol.Equals("ftpx"))
    sep = "?;";
  else if (!m_strProtocol.Equals("flash"))
    sep = "?";

  if(sep)
  {
    int iOptions = strURL.find_first_of(sep, iPos);
    if (iOptions >= 0 )
    {
      // we keep the initial char as it can be any of the above
      int iProto = strURL.find_first_of("|",iOptions);
      if (iProto >= 0)
      {
        m_strProtocolOptions = strURL.substr(iProto+1);
        m_strOptions = strURL.substr(iOptions,iProto-iOptions);
      }
      else
      m_strOptions = strURL.substr(iOptions);
      iEnd = iOptions;
      m_strWithoutOptions = strURL.substr(0,iOptions);
    }
  }

  int iSlash = strURL.Find("/", iPos);
  if(iSlash >= iEnd)
    iSlash = -1; // was an invalid slash as it was contained in options

  if( !m_strProtocol.Equals("iso9660") )
  {
    int iAlphaSign = strURL.Find("@", iPos);
    if (iAlphaSign >= 0 && iAlphaSign < iEnd && (iAlphaSign < iSlash || iSlash < 0))
    {
      // username/password found
      CStdString strUserNamePassword = strURL.Mid(iPos, iAlphaSign - iPos);

      // first extract domain, if protocol is smb
      if (m_strProtocol.Equals("smb"))
      {
        int iSemiColon = strUserNamePassword.Find(";");

        if (iSemiColon >= 0)
        {
          m_strDomain = strUserNamePassword.Left(iSemiColon);
          strUserNamePassword.Delete(0, iSemiColon + 1);
        }
      }

      // username:password
      int iColon = strUserNamePassword.Find(":");
      if (iColon >= 0)
      {
        m_strUserName = strUserNamePassword.Left(iColon);
        iColon++;
        m_strPassword = strUserNamePassword.Right(strUserNamePassword.size() - iColon);
      }
      // username
      else
      {
        m_strUserName = strUserNamePassword;
      }

      iPos = iAlphaSign + 1;
      iSlash = strURL.Find("/", iAlphaSign);

      if(iSlash >= iEnd)
        iSlash = -1;
    }
  }

  // detect hostname:port/
  if (iSlash < 0)
  {
    CStdString strHostNameAndPort = strURL.Mid(iPos, iEnd - iPos);
    int iColon = strHostNameAndPort.Find(":");
    if (iColon >= 0)
    {
      m_strHostName = strHostNameAndPort.Left(iColon);
      iColon++;
      CStdString strPort = strHostNameAndPort.Right(strHostNameAndPort.size() - iColon);
      m_iPort = atoi(strPort.c_str());
    }
    else
    {
      m_strHostName = strHostNameAndPort;
    }

  }
  else
  {
    CStdString strHostNameAndPort = strURL.Mid(iPos, iSlash - iPos);
    int iColon = strHostNameAndPort.Find(":");
    if (iColon >= 0)
    {
      m_strHostName = strHostNameAndPort.Left(iColon);
      iColon++;
      CStdString strPort = strHostNameAndPort.Right(strHostNameAndPort.size() - iColon);
      m_iPort = atoi(strPort.c_str());
    }
    else
    {
      m_strHostName = strHostNameAndPort;
    }
    iPos = iSlash + 1;
    if (iEnd > iPos)
    {
      m_strFileName = strURL.Mid(iPos, iEnd - iPos);

      iSlash = m_strFileName.Find("/");
      if(iSlash < 0)
        m_strShareName = m_strFileName;
      else
        m_strShareName = m_strFileName.Left(iSlash);
    }
  }

  // iso9960 doesnt have an hostname;-)
  if (m_strProtocol.CompareNoCase("iso9660") == 0
    || m_strProtocol.CompareNoCase("musicdb") == 0
    || m_strProtocol.CompareNoCase("videodb") == 0
    || m_strProtocol.CompareNoCase("lastfm") == 0
    || m_strProtocol.Left(3).CompareNoCase("mem") == 0)
  {
    if (m_strHostName != "" && m_strFileName != "")
    {
      CStdString strFileName = m_strFileName;
      m_strFileName.Format("%s/%s", m_strHostName.c_str(), strFileName.c_str());
      m_strHostName = "";
    }
    else
    {
      if (!m_strHostName.IsEmpty() && strURL[iEnd-1]=='/')
        m_strFileName=m_strHostName + "/";
      else
        m_strFileName = m_strHostName;
      m_strHostName = "";
    }
  }

  m_strFileName.Replace("\\", "/");

  /* update extension */
  SetFileName(m_strFileName);

  /* decode urlencoding on this stuff */
  if( m_strProtocol.Equals("rar") || m_strProtocol.Equals("zip") || m_strProtocol.Equals("musicsearch"))
  {
    CUtil::UrlDecode(m_strHostName);
    // Validate it as it is likely to contain a filename
    SetHostName(ValidatePath(m_strHostName));
  }

  CUtil::UrlDecode(m_strUserName);
  CUtil::UrlDecode(m_strPassword);
}

CURI::CURI(const CURI &url)
{
  *this = url;
}

CURI::CURI()
{
  m_iPort = 0;
}

CURI::~CURI()
{
}

CURI& CURI::operator= (const CURI& source)
{
  m_iPort        = source.m_iPort;
  m_strHostName  = source.m_strHostName;
  m_strDomain    = source.m_strDomain;
  m_strShareName = source.m_strShareName;
  m_strUserName  = source.m_strUserName;
  m_strPassword  = source.m_strPassword;
  m_strFileName  = source.m_strFileName;
  m_strProtocol  = source.m_strProtocol;
  m_strFileType  = source.m_strFileType;
  m_strOptions   = source.m_strOptions;
  m_strProtocolOptions = source.m_strProtocolOptions;
  return *this;
}

void CURI::SetFileName(const CStdString& strFileName)
{
  m_strFileName = strFileName;

  int slash = m_strFileName.find_last_of(GetDirectorySeparator());
  int period = m_strFileName.find_last_of('.');
  if(period != -1 && (slash == -1 || period > slash))
    m_strFileType = m_strFileName.substr(period+1);
  else
    m_strFileType = "";

  m_strFileType.Normalize();
}

void CURI::SetHostName(const CStdString& strHostName)
{
  m_strHostName = strHostName;
}

void CURI::SetUserName(const CStdString& strUserName)
{
  m_strUserName = strUserName;
}

void CURI::SetPassword(const CStdString& strPassword)
{
  m_strPassword = strPassword;
}

void CURI::SetProtocol(const CStdString& strProtocol)
{
  m_strProtocol = strProtocol;
}

void CURI::SetOptions(const CStdString& strOptions)
{
  m_strOptions.Empty();
  if( strOptions.length() > 0)
  {
    if( strOptions[0] == '?' || strOptions[0] == '#' || strOptions[0] == ';' || strOptions.Find("xml") >=0 )
    {
      m_strOptions = strOptions;
    }
    else
      CLog::Log(LOGWARNING, "%s - Invalid options specified for url %s", __FUNCTION__, strOptions.c_str());
}
}

void CURI::SetProtocolOptions(const CStdString& strOptions)
{
  m_strProtocolOptions = strOptions;
}

void CURI::SetPort(int port)
{
  m_iPort = port;
}

bool CURI::HasPort() const
{
  return (m_iPort != 0);
}

int CURI::GetPort() const
{
  return m_iPort;
}


const CStdString& CURI::GetHostName() const
{
  return m_strHostName;
}

const CStdString&  CURI::GetShareName() const
{
  return m_strShareName;
}

const CStdString& CURI::GetDomain() const
{
  return m_strDomain;
}

const CStdString& CURI::GetUserName() const
{
  return m_strUserName;
}

const CStdString& CURI::GetPassWord() const
{
  return m_strPassword;
}

const CStdString& CURI::GetFileName() const
{
  return m_strFileName;
}

const CStdString& CURI::GetProtocol() const
{
  return m_strProtocol;
}

const CStdString& CURI::GetFileType() const
{
  return m_strFileType;
}

const CStdString& CURI::GetOptions() const
{
  return m_strOptions;
}

const CStdString& CURI::GetUrlWithoutOptions() const
{
  return m_strWithoutOptions;
}

const CStdString CURI::GetProtocolOptions() const
{
  return m_strProtocolOptions;
}

const CStdString CURI::GetFileNameWithoutPath() const
{
  // *.zip and *.rar store the actual zip/rar path in the hostname of the url
  if ((m_strProtocol == "rar" || m_strProtocol == "zip") && m_strFileName.IsEmpty())
    return CUtil::GetFileName(m_strHostName);

  // otherwise, we've already got the filepath, so just grab the filename portion
  CStdString file(m_strFileName);
  CUtil::RemoveSlashAtEnd(file);
  return CUtil::GetFileName(file);
}

char CURI::GetDirectorySeparator() const
{
#ifndef _LINUX
  if ( IsLocal() )
    return '\\';
  else
#endif
    return '/';
}

CStdString CURI::Get() const
{
  unsigned int sizeneed = m_strProtocol.length()
                        + m_strDomain.length()
                        + m_strUserName.length()
                        + m_strPassword.length()
                        + m_strHostName.length()
                        + m_strFileName.length()
                        + m_strOptions.length()
                        + m_strProtocolOptions.length()
                        + 10;

  if (m_strProtocol == "")
    return m_strFileName;

  CStdString strURL;
  strURL.reserve(sizeneed);

  strURL = GetWithoutFilename();
  strURL += m_strFileName;

  if( m_strOptions.length() > 0 )
    strURL += m_strOptions;
  if (m_strProtocolOptions.length() > 0)
    strURL += "|"+m_strProtocolOptions;

  return strURL;
}

CStdString CURI::GetWithoutUserDetails() const
{
  CStdString strURL;

  if (m_strProtocol.Equals("stack"))
  {
    CFileItemList items;
    CStdString strURL2;
    strURL2 = Get();
    DIRECTORY::CStackDirectory dir;
    dir.GetDirectory(strURL2,items);
    vector<CStdString> newItems;
    for (int i=0;i<items.Size();++i)
    {
      CURI url(items[i]->m_strPath);
      items[i]->m_strPath = url.GetWithoutUserDetails();
      newItems.push_back(items[i]->m_strPath);
    }
    dir.ConstructStackPath(newItems,strURL);
    return strURL;
  }

  unsigned int sizeneed = m_strProtocol.length()
                        + m_strDomain.length()
                        + m_strHostName.length()
                        + m_strFileName.length()
                        + m_strOptions.length()
                        + m_strProtocolOptions.length()
                        + 10;

  strURL.reserve(sizeneed);

  if (m_strProtocol == "")
    return m_strFileName;

  strURL = m_strProtocol;
  strURL += "://";

  if (m_strHostName != "")
  {
    if (m_strProtocol.Equals("rar") || m_strProtocol.Equals("zip"))
      strURL += CURI(m_strHostName).GetWithoutUserDetails();
    else
      strURL += m_strHostName;

    if ( HasPort() )
    {
      CStdString strPort;
      strPort.Format("%i", m_iPort);
      strURL += ":";
      strURL += strPort;
    }
    strURL += "/";
  }
  strURL += m_strFileName;

  if( m_strOptions.length() > 0 )
    strURL += m_strOptions;
  if( m_strProtocolOptions.length() > 0 )
    strURL += "|"+m_strProtocolOptions;

  return strURL;
}

CStdString CURI::GetWithoutFilename() const
{
  if (m_strProtocol == "")
    return "";

  unsigned int sizeneed = m_strProtocol.length()
                        + m_strDomain.length()
                        + m_strUserName.length()
                        + m_strPassword.length()
                        + m_strHostName.length()
                        + 10;

  CStdString strURL;
  strURL.reserve(sizeneed);

  strURL = m_strProtocol;
  strURL += "://";

  if (m_strDomain != "")
  {
    strURL += m_strDomain;
    strURL += ";";
  }
  else if (m_strUserName != "")
  {
    strURL += URLEncodeInline(m_strUserName);
    if (m_strPassword != "")
    {
      strURL += ":";
      strURL += URLEncodeInline(m_strPassword);
    }
    strURL += "@";
  }
  else if (m_strDomain != "")
    strURL += "@";

  if (m_strHostName != "")
  {
    if( m_strProtocol.Equals("rar") || m_strProtocol.Equals("zip") || m_strProtocol.Equals("musicsearch"))
      strURL += URLEncodeInline(m_strHostName);
    else
      strURL += m_strHostName;
    if (HasPort())
    {
      CStdString strPort;
      strPort.Format("%i", m_iPort);
      strURL += ":";
      strURL += strPort;
    }
    strURL += "/";
  }

  return strURL;
}

bool CURI::IsLocal() const
{
  return m_strProtocol.IsEmpty() || m_strProtocol == "special";
}

bool CURI::IsFileOnly(const CStdString &url)
{
  return url.find_first_of("/\\") == CStdString::npos;
}

const std::map<CStdString, CStdString> CURI::GetOptionsAsMap() const
{
  std::map<CStdString, CStdString> result;
  CStdString options = m_strOptions;
  
  if (m_strOptions.Left(1) == "?")
  {
    options = m_strOptions.Right(m_strOptions.size() - 1);
  }
  

  if (options == "")
  {
    return result;
  }
  
  int equalPos;
  std::vector<CStdString> tokens;
  CUtil::Tokenize(options, tokens, "&");
  
  for (size_t i = 0; i < tokens.size(); i++)
  {
    equalPos = tokens[i].Find("=");
    CStdString key = tokens[i].Left(equalPos);
    CStdString value = tokens[i].Right(tokens[i].size() - equalPos - 1);
    CUtil::UrlDecode(value);
    result[key] = value;
  }
  
  return result;
}

bool CURI::IsFullPath(const CStdString &url)
{
  if (url.size() && url[0] == '/') return true;     //   /foo/bar.ext
  if (url.Find("://") >= 0) return true;                 //   foo://bar.ext
  if (url.size() > 1 && url[1] == ':') return true; //   c:\\foo\\bar\\bar.ext
  return false;
}

CStdString CURI::ValidatePath(const CStdString &path)
{
  CStdString result = path;
  
  // Don't do any stuff on URLs containing %-characters as we may screw up
  // URL-encoded (embedded) filenames (like with zip:// & rar://)
  if (path.Find("://") >= 0 && path.Find('%') >= 0)
    return result;
   
  // check the path for incorrect slashes
#ifdef _WIN32
  if (CUtil::IsDOSPath(path))
  {
    result.Replace('/', '\\');
    // Fixup for double back slashes (but ignore the \\ of unc-paths) 
    for (int x=1; x<result.GetLength()-1; x++) 
    { 
      if (result[x] == '\\' && result[x+1] == '\\') 
        result.Delete(x);   
    }    
  }
  else if (path.Find("://") >= 0 || path.Find(":\\\\") >= 0)
  {
    result.Replace('\\', '/');
    // Fixup for double forward slashes(/) but don't touch the :// of URLs
    for (int x=1; x<result.GetLength()-1; x++) 
    { 
      if (result[x] == '/' && result[x+1] == '/' && result[x-1] != ':') 
        result.Delete(x); 
    }        
  }
#else
  result.Replace('\\', '/');
  // Fixup for double forward slashes(/) but don't touch the :// of URLs 
  for (int x=1; x<result.GetLength()-1; x++) 
  { 
    if (result[x] == '/' && result[x+1] == '/' && result[x-1] != ':') 
      result.Delete(x); 
  }        
#endif
  return result;
}

