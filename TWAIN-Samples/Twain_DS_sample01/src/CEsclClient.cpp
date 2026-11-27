#include "CEsclClient.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
extern HINSTANCE g_hinstance;
#endif

using json = nlohmann::json;

namespace
{
  const int kTwptBw   = 0;
  const int kTwptGray = 1;
  const int kTwptRgb  = 2;

  std::string trimString(const std::string& s)
  {
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start]))
    {
      ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1]))
    {
      --end;
    }
    return s.substr(start, end - start);
  }

  std::string toLowerAscii(std::string s)
  {
    for (size_t i = 0; i < s.size(); ++i)
    {
      s[i] = (char)std::tolower((unsigned char)s[i]);
    }
    return s;
  }

  bool startsWithNoCase(const std::string& s, const std::string& prefix)
  {
    if (s.size() < prefix.size())
    {
      return false;
    }
    return toLowerAscii(s.substr(0, prefix.size())) == toLowerAscii(prefix);
  }

  bool endsWithNoCase(const std::string& s, const std::string& suffix)
  {
    if (s.size() < suffix.size())
    {
      return false;
    }
    return toLowerAscii(s.substr(s.size() - suffix.size())) == toLowerAscii(suffix);
  }

  std::string stripTrailingSlashes(std::string s)
  {
    while (!s.empty() && s[s.size() - 1] == '/')
    {
      s.erase(s.size() - 1);
    }
    return s;
  }

#ifdef _WIN32
  std::wstring utf8ToWide(const std::string& utf8)
  {
    if (utf8.empty())
    {
      return std::wstring();
    }
    int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
    {
      return std::wstring();
    }
    std::wstring out((size_t)len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &out[0], len);
    return out;
  }

  std::string wideToUtf8(const std::wstring& wide)
  {
    if (wide.empty())
    {
      return std::string();
    }
    int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), NULL, 0, NULL, NULL);
    if (len <= 0)
    {
      return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &out[0], len, NULL, NULL);
    return out;
  }

  std::wstring getDataSourceDirW()
  {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = ::GetModuleFileNameW((HMODULE)g_hinstance, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]) - 1));
    if (len == 0)
    {
      return std::wstring();
    }
    std::wstring full(buf, len);
    size_t pos = full.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : full.substr(0, pos);
  }

  std::wstring getUserSettingsDirW()
  {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (len == 0 || len >= (sizeof(buf) / sizeof(buf[0])))
    {
      len = ::GetTempPathW((DWORD)(sizeof(buf) / sizeof(buf[0])), buf);
      if (len == 0)
      {
        return std::wstring();
      }
    }
    std::wstring dir(buf, len);
    if (!dir.empty() && dir[dir.size() - 1] != L'\\' && dir[dir.size() - 1] != L'/')
    {
      dir += L'\\';
    }
    dir += L"VirtualScanner";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
  }

  std::wstring getTempScanDirW()
  {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = ::GetTempPathW((DWORD)(sizeof(buf) / sizeof(buf[0])), buf);
    if (len == 0 || len >= (sizeof(buf) / sizeof(buf[0])))
    {
      return std::wstring();
    }
    std::wstring dir(buf, len);
    if (!dir.empty() && dir[dir.size() - 1] != L'\\' && dir[dir.size() - 1] != L'/')
    {
      dir += L'\\';
    }
    dir += L"VirtualScanner";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
  }

  bool readFileUtf8(const std::wstring& path, std::string& outContent)
  {
    outContent.clear();
    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
      return false;
    }
    LARGE_INTEGER fileSize;
    if (!::GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 10 * 1024 * 1024)
    {
      ::CloseHandle(hFile);
      return false;
    }
    outContent.resize((size_t)fileSize.QuadPart);
    DWORD bytesRead = 0;
    BOOL ok = ::ReadFile(hFile, &outContent[0], (DWORD)fileSize.QuadPart, &bytesRead, NULL);
    ::CloseHandle(hFile);
    if (!ok)
    {
      outContent.clear();
      return false;
    }
    outContent.resize(bytesRead);
    return true;
  }

  bool writeBytesToFile(const std::wstring& path, const char* data, size_t size)
  {
    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
      return false;
    }
    size_t offset = 0;
    while (offset < size)
    {
      DWORD chunk = (DWORD)(std::min)((size_t)65536, size - offset);
      DWORD written = 0;
      if (!::WriteFile(hFile, data + offset, chunk, &written, NULL) || written == 0)
      {
        ::CloseHandle(hFile);
        ::DeleteFileW(path.c_str());
        return false;
      }
      offset += written;
    }
    ::CloseHandle(hFile);
    return true;
  }

  struct HttpResponse
  {
    bool        transportOk;
    bool        cancelled;
    DWORD       statusCode;
    std::string location;
    std::string contentType;
    std::string body;
    std::string error;

    HttpResponse()
      : transportOk(false)
      , cancelled(false)
      , statusCode(0)
    {
    }
  };

  std::string queryWinHttpHeader(HINTERNET hRequest, DWORD queryFlag)
  {
    DWORD size = 0;
    ::WinHttpQueryHeaders(hRequest, queryFlag, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &size, WINHTTP_NO_HEADER_INDEX);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
    {
      return std::string();
    }
    std::wstring buf((size / sizeof(wchar_t)) + 1, L'\0');
    if (!::WinHttpQueryHeaders(hRequest, queryFlag, WINHTTP_HEADER_NAME_BY_INDEX, &buf[0], &size, WINHTTP_NO_HEADER_INDEX))
    {
      return std::string();
    }
    size_t chars = size / sizeof(wchar_t);
    while (chars > 0 && buf[chars - 1] == L'\0')
    {
      --chars;
    }
    buf.resize(chars);
    return trimString(wideToUtf8(buf));
  }

  std::string formatWin32Error(DWORD err)
  {
    std::ostringstream oss;
    switch (err)
    {
      case ERROR_WINHTTP_TIMEOUT:
        return "Connection timed out";
      case ERROR_WINHTTP_CANNOT_CONNECT:
        return "Could not connect to scanner";
      case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "Could not resolve scanner host name";
      case ERROR_WINHTTP_CONNECTION_ERROR:
        return "Network connection error";
      case ERROR_WINHTTP_OPERATION_CANCELLED:
        return "Operation cancelled";
      default:
        oss << "HTTP error (WinHTTP code " << err << ")";
        return oss.str();
    }
  }

  struct ParsedUrl
  {
    bool        valid;
    bool        isHttps;
    std::string scheme;
    std::string host;
    INTERNET_PORT port;
    std::string path;

    ParsedUrl()
      : valid(false)
      , isHttps(false)
      , port(80)
    {
    }
  };

  ParsedUrl parseUrl(const std::string& url)
  {
    ParsedUrl out;
    std::wstring wurl = utf8ToWide(url);
    if (wurl.empty())
    {
      return out;
    }

    wchar_t hostBuf[512] = {0};
    wchar_t pathBuf[2048] = {0};
    wchar_t extraBuf[1024] = {0};

    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = hostBuf;
    uc.dwHostNameLength  = (DWORD)(sizeof(hostBuf) / sizeof(hostBuf[0]));
    uc.lpszUrlPath       = pathBuf;
    uc.dwUrlPathLength   = (DWORD)(sizeof(pathBuf) / sizeof(pathBuf[0]));
    uc.lpszExtraInfo     = extraBuf;
    uc.dwExtraInfoLength = (DWORD)(sizeof(extraBuf) / sizeof(extraBuf[0]));

    if (!::WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
    {
      return out;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
    {
      return out;
    }

    out.valid   = (uc.dwHostNameLength > 0);
    out.isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out.scheme  = out.isHttps ? "https" : "http";
    out.host    = wideToUtf8(std::wstring(hostBuf, uc.dwHostNameLength));
    out.port    = uc.nPort;
    out.path    = wideToUtf8(std::wstring(pathBuf, uc.dwUrlPathLength)) +
                  wideToUtf8(std::wstring(extraBuf, uc.dwExtraInfoLength));
    if (out.path.empty())
    {
      out.path = "/";
    }
    return out;
  }

  std::string buildOrigin(const ParsedUrl& u)
  {
    std::ostringstream oss;
    oss << u.scheme << "://" << u.host;
    if ((!u.isHttps && u.port != 80) || (u.isHttps && u.port != 443))
    {
      oss << ":" << u.port;
    }
    return oss.str();
  }

  HttpResponse performHttpRequest(
    const std::string& method,
    const std::string& url,
    const std::string& contentType,
    const std::string& requestBody,
    int connectTimeoutMs,
    int receiveTimeoutMs,
    const std::shared_ptr<EsclCancelToken>& cancelToken)
  {
    HttpResponse resp;
    if (cancelToken && cancelToken->isCancelled())
    {
      resp.cancelled = true;
      resp.error = "Scan cancelled.";
      return resp;
    }

    ParsedUrl parsed = parseUrl(url);
    if (!parsed.valid)
    {
      resp.error = "Invalid scanner URL: " + url;
      return resp;
    }

    HINTERNET hSession = ::WinHttpOpen(
      L"VirtualScanner-eSCL/1.0",
      WINHTTP_ACCESS_TYPE_NO_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);
    if (!hSession)
    {
      resp.error = formatWin32Error(::GetLastError());
      return resp;
    }

    ::WinHttpSetTimeouts(hSession, connectTimeoutMs, connectTimeoutMs, 15000, receiveTimeoutMs);

    std::wstring wHost = utf8ToWide(parsed.host);
    HINTERNET hConnect = ::WinHttpConnect(hSession, wHost.c_str(), parsed.port, 0);
    if (!hConnect)
    {
      resp.error = formatWin32Error(::GetLastError());
      ::WinHttpCloseHandle(hSession);
      return resp;
    }

    std::wstring wMethod = utf8ToWide(method);
    std::wstring wPath   = utf8ToWide(parsed.path);
    DWORD flags = parsed.isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = ::WinHttpOpenRequest(
      hConnect,
      wMethod.c_str(),
      wPath.c_str(),
      NULL,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      flags);

    if (!hRequest)
    {
      resp.error = formatWin32Error(::GetLastError());
      ::WinHttpCloseHandle(hConnect);
      ::WinHttpCloseHandle(hSession);
      return resp;
    }

    if (parsed.isHttps)
    {
      DWORD secFlags =
        SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
      ::WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    if (cancelToken && !cancelToken->setActiveHandle(hRequest))
    {
      ::WinHttpCloseHandle(hRequest);
      ::WinHttpCloseHandle(hConnect);
      ::WinHttpCloseHandle(hSession);
      resp.cancelled = true;
      resp.error = "Scan cancelled.";
      return resp;
    }

    std::wstring wHeaders;
    if (!contentType.empty())
    {
      wHeaders = L"Content-Type: " + utf8ToWide(contentType) + L"\r\n";
    }

    LPVOID pBody = requestBody.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)requestBody.data();
    DWORD  bodyLen = (DWORD)requestBody.size();

    BOOL ok = ::WinHttpSendRequest(
      hRequest,
      wHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wHeaders.c_str(),
      wHeaders.empty() ? 0 : (DWORD)-1L,
      pBody,
      bodyLen,
      bodyLen,
      0);

    if (ok)
    {
      ok = ::WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!ok)
    {
      DWORD err = ::GetLastError();
      bool wasCancelled = (cancelToken && cancelToken->isCancelled()) ||
                          (err == ERROR_WINHTTP_OPERATION_CANCELLED);
      if (!cancelToken || cancelToken->clearActiveHandle(hRequest))
      {
        ::WinHttpCloseHandle(hRequest);
      }
      ::WinHttpCloseHandle(hConnect);
      ::WinHttpCloseHandle(hSession);
      resp.cancelled = wasCancelled;
      resp.error = wasCancelled ? "Scan cancelled." : formatWin32Error(err);
      return resp;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (::WinHttpQueryHeaders(
          hRequest,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,
          &statusCode,
          &statusSize,
          WINHTTP_NO_HEADER_INDEX))
    {
      resp.statusCode = statusCode;
    }

    resp.location    = queryWinHttpHeader(hRequest, WINHTTP_QUERY_LOCATION);
    resp.contentType = queryWinHttpHeader(hRequest, WINHTTP_QUERY_CONTENT_TYPE);

    bool readOk = true;
    const size_t kMaxBodyBytes = 128 * 1024 * 1024;
    for (;;)
    {
      if (cancelToken && cancelToken->isCancelled())
      {
        readOk = false;
        resp.cancelled = true;
        resp.error = "Scan cancelled.";
        break;
      }

      DWORD avail = 0;
      if (!::WinHttpQueryDataAvailable(hRequest, &avail))
      {
        DWORD err = ::GetLastError();
        readOk = false;
        resp.cancelled = (cancelToken && cancelToken->isCancelled()) ||
                         (err == ERROR_WINHTTP_OPERATION_CANCELLED);
        resp.error = resp.cancelled ? "Scan cancelled." : formatWin32Error(err);
        break;
      }

      if (avail == 0)
      {
        break;
      }

      if (resp.body.size() + avail > kMaxBodyBytes)
      {
        readOk = false;
        resp.error = "Scanned response exceeded maximum size limit.";
        break;
      }

      size_t prevSize = resp.body.size();
      resp.body.resize(prevSize + avail);
      DWORD readBytes = 0;
      if (!::WinHttpReadData(hRequest, &resp.body[prevSize], avail, &readBytes))
      {
        DWORD err = ::GetLastError();
        readOk = false;
        resp.cancelled = (cancelToken && cancelToken->isCancelled()) ||
                         (err == ERROR_WINHTTP_OPERATION_CANCELLED);
        resp.error = resp.cancelled ? "Scan cancelled." : formatWin32Error(err);
        break;
      }
      resp.body.resize(prevSize + readBytes);
      if (readBytes == 0)
      {
        break;
      }
    }

    if (!cancelToken || cancelToken->clearActiveHandle(hRequest))
    {
      ::WinHttpCloseHandle(hRequest);
    }
    ::WinHttpCloseHandle(hConnect);
    ::WinHttpCloseHandle(hSession);

    resp.transportOk = readOk;
    return resp;
  }

  // Namespace-agnostic XML helpers for eSCL ScannerCapabilities
  std::string extractXmlBlock(const std::string& xml, const std::string& localTag, size_t startPos = 0, size_t* outNextPos = NULL)
  {
    size_t pos = startPos;
    while (pos < xml.size())
    {
      size_t lt = xml.find('<', pos);
      if (lt == std::string::npos)
      {
        break;
      }
      if (lt + 1 < xml.size() && (xml[lt + 1] == '/' || xml[lt + 1] == '?' || xml[lt + 1] == '!'))
      {
        pos = lt + 1;
        continue;
      }

      size_t gt = xml.find('>', lt + 1);
      if (gt == std::string::npos)
      {
        break;
      }

      // Extract full opening tag name (up to whitespace, '/', or '>')
      size_t nameEnd = lt + 1;
      while (nameEnd < gt && !std::isspace((unsigned char)xml[nameEnd]) && xml[nameEnd] != '/')
      {
        ++nameEnd;
      }
      std::string fullName = xml.substr(lt + 1, nameEnd - (lt + 1));
      std::string tagLocal = fullName;
      size_t colon = fullName.find(':');
      if (colon != std::string::npos)
      {
        tagLocal = fullName.substr(colon + 1);
      }

      if (tagLocal == localTag)
      {
        // Self-closing tag
        if (gt > lt + 1 && xml[gt - 1] == '/')
        {
          if (outNextPos)
          {
            *outNextPos = gt + 1;
          }
          return std::string();
        }

        // Find matching closing tag </fullName> (or </*:localTag>)
        std::string exactClose = "</" + fullName + ">";
        size_t closePos = xml.find(exactClose, gt + 1);
        size_t closeEnd = (closePos != std::string::npos) ? (closePos + exactClose.size()) : std::string::npos;

        if (closePos == std::string::npos)
        {
          std::string suffixClose = ":" + localTag + ">";
          size_t search = gt + 1;
          while (search < xml.size())
          {
            size_t c = xml.find("</", search);
            if (c == std::string::npos)
            {
              break;
            }
            size_t cgt = xml.find('>', c + 2);
            if (cgt == std::string::npos)
            {
              break;
            }
            std::string closeName = xml.substr(c + 2, cgt - (c + 2));
            size_t cColon = closeName.find(':');
            std::string closeLocal = (cColon != std::string::npos) ? closeName.substr(cColon + 1) : closeName;
            if (closeLocal == localTag)
            {
              closePos = c;
              closeEnd = cgt + 1;
              break;
            }
            search = cgt + 1;
          }
        }

        if (closePos != std::string::npos)
        {
          if (outNextPos)
          {
            *outNextPos = closeEnd;
          }
          return xml.substr(gt + 1, closePos - (gt + 1));
        }
        break;
      }

      pos = gt + 1;
    }

    if (outNextPos)
    {
      *outNextPos = std::string::npos;
    }
    return std::string();
  }

  std::vector<std::string> extractAllXmlValues(const std::string& xml, const std::string& localTag)
  {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos != std::string::npos && pos < xml.size())
    {
      size_t nextPos = std::string::npos;
      std::string val = trimString(extractXmlBlock(xml, localTag, pos, &nextPos));
      if (nextPos == std::string::npos)
      {
        break;
      }
      if (!val.empty())
      {
        out.push_back(val);
      }
      pos = nextPos;
    }
    return out;
  }

  int extractXmlInt(const std::string& xml, const std::string& localTag, int fallback)
  {
    std::string val = trimString(extractXmlBlock(xml, localTag));
    if (val.empty())
    {
      return fallback;
    }
    char* endPtr = NULL;
    long parsed = std::strtol(val.c_str(), &endPtr, 10);
    if (endPtr == val.c_str() || parsed <= 0)
    {
      return fallback;
    }
    return (int)parsed;
  }

  struct EsclInputCaps
  {
    bool present;
    int  minWidth;
    int  maxWidth;
    int  minHeight;
    int  maxHeight;
    std::vector<int> resolutions;
    bool supportsRgb24;
    bool supportsGray8;
    bool supportsJpeg;
    bool supportsPdf;
    bool supportsTiff;
    bool supportsIntentDocument;

    EsclInputCaps()
      : present(false)
      , minWidth(300)
      , maxWidth(2550)
      , minHeight(300)
      , maxHeight(3508)
      , supportsRgb24(true)
      , supportsGray8(true)
      , supportsJpeg(true)
      , supportsPdf(false)
      , supportsTiff(false)
      , supportsIntentDocument(false)
    {
    }
  };

  struct EsclCapabilities
  {
    std::string   version;
    std::string   makeAndModel;
    EsclInputCaps platen;
    EsclInputCaps adfSimplex;
    EsclInputCaps adfDuplex;

    EsclCapabilities()
      : version("2.0")
    {
    }
  };

  void parseInputCapsBlock(const std::string& block, EsclInputCaps& out)
  {
    if (block.empty())
    {
      return;
    }
    out.present   = true;
    out.minWidth  = extractXmlInt(block, "MinWidth", 300);
    out.maxWidth  = extractXmlInt(block, "MaxWidth", 2550);
    out.minHeight = extractXmlInt(block, "MinHeight", 300);
    out.maxHeight = extractXmlInt(block, "MaxHeight", 3508);

    std::vector<std::string> xRes = extractAllXmlValues(block, "XResolution");
    out.resolutions.clear();
    for (size_t i = 0; i < xRes.size(); ++i)
    {
      int dpi = std::atoi(xRes[i].c_str());
      if (dpi >= 50 && dpi <= 2400 &&
          std::find(out.resolutions.begin(), out.resolutions.end(), dpi) == out.resolutions.end())
      {
        out.resolutions.push_back(dpi);
      }
    }
    std::sort(out.resolutions.begin(), out.resolutions.end());

    std::vector<std::string> modes = extractAllXmlValues(block, "ColorMode");
    if (!modes.empty())
    {
      out.supportsRgb24 = false;
      out.supportsGray8 = false;
      for (size_t i = 0; i < modes.size(); ++i)
      {
        if (modes[i] == "RGB24")
        {
          out.supportsRgb24 = true;
        }
        else if (modes[i] == "Grayscale8")
        {
          out.supportsGray8 = true;
        }
      }
    }

    std::vector<std::string> fmts = extractAllXmlValues(block, "DocumentFormat");
    if (!fmts.empty())
    {
      out.supportsJpeg = false;
      out.supportsPdf  = false;
      out.supportsTiff = false;
      for (size_t i = 0; i < fmts.size(); ++i)
      {
        std::string f = toLowerAscii(fmts[i]);
        if (f == "image/jpeg")
        {
          out.supportsJpeg = true;
        }
        else if (f == "application/pdf")
        {
          out.supportsPdf = true;
        }
        else if (f == "image/tiff")
        {
          out.supportsTiff = true;
        }
      }
    }

    std::vector<std::string> intents = extractAllXmlValues(block, "Intent");
    out.supportsIntentDocument = false;
    for (size_t i = 0; i < intents.size(); ++i)
    {
      if (intents[i] == "Document")
      {
        out.supportsIntentDocument = true;
        break;
      }
    }
  }

  EsclCapabilities parseCapabilitiesXml(const std::string& xml)
  {
    EsclCapabilities caps;
    std::string ver = trimString(extractXmlBlock(xml, "Version"));
    if (!ver.empty())
    {
      caps.version = ver;
    }
    caps.makeAndModel = trimString(extractXmlBlock(xml, "MakeAndModel"));

    parseInputCapsBlock(extractXmlBlock(xml, "PlatenInputCaps"), caps.platen);
    parseInputCapsBlock(extractXmlBlock(xml, "AdfSimplexInputCaps"), caps.adfSimplex);
    parseInputCapsBlock(extractXmlBlock(xml, "AdfDuplexInputCaps"), caps.adfDuplex);
    return caps;
  }

  std::vector<std::string> buildCandidateBaseUrls(const std::string& rawTarget)
  {
    std::vector<std::string> out;
    std::string target = stripTrailingSlashes(trimString(rawTarget));
    if (target.empty())
    {
      return out;
    }

    auto addUnique = [&out](const std::string& u) {
      if (!u.empty() && std::find(out.begin(), out.end(), u) == out.end())
      {
        out.push_back(u);
      }
    };

    if (startsWithNoCase(target, "http://") || startsWithNoCase(target, "https://"))
    {
      if (endsWithNoCase(target, "/ScannerCapabilities"))
      {
        target = target.substr(0, target.size() - std::string("/ScannerCapabilities").size());
      }
      else if (endsWithNoCase(target, "/ScanJobs"))
      {
        target = target.substr(0, target.size() - std::string("/ScanJobs").size());
      }
      target = stripTrailingSlashes(target);

      std::string lower = toLowerAscii(target);
      if (lower.find("/escl") != std::string::npos)
      {
        addUnique(target);
      }
      else
      {
        addUnique(target + "/eSCL");
        addUnique(target);
      }
      return out;
    }

    // Bare IP or host[:port]
    bool hasPort = (target.find(':') != std::string::npos);
    if (hasPort)
    {
      addUnique("http://" + target + "/eSCL");
      addUnique("https://" + target + "/eSCL");
    }
    else
    {
      addUnique("http://" + target + "/eSCL");
      addUnique("http://" + target + ":8080/eSCL");
      addUnique("https://" + target + "/eSCL");
    }
    return out;
  }

  int pickNearestResolution(const std::vector<int>& supported, int desired)
  {
    if (supported.empty())
    {
      return (desired > 0) ? desired : 200;
    }
    int best = supported[0];
    int bestDiff = std::abs(supported[0] - desired);
    for (size_t i = 1; i < supported.size(); ++i)
    {
      int diff = std::abs(supported[i] - desired);
      if (diff < bestDiff)
      {
        best = supported[i];
        bestDiff = diff;
      }
    }
    return best;
  }

  void computePaperDimensions(EsclPaperSize paper, const EsclInputCaps& caps, int& outW, int& outH)
  {
    // Units are eSCL ThreeHundredthsOfInches (300 units per inch)
    switch (paper)
    {
      case ESCL_PAPER_A5:
        outW = 1748; // 148 mm
        outH = 2480; // 210 mm
        break;
      case ESCL_PAPER_LETTER:
        outW = 2550; // 8.5 in
        outH = 3300; // 11.0 in
        break;
      case ESCL_PAPER_MAX_GLASS:
        outW = caps.maxWidth;
        outH = caps.maxHeight;
        break;
      case ESCL_PAPER_A4:
      default:
        outW = 2480; // 210 mm
        outH = 3508; // 297 mm
        break;
    }

    if (outW < caps.minWidth)  outW = caps.minWidth;
    if (outW > caps.maxWidth)  outW = caps.maxWidth;
    if (outH < caps.minHeight) outH = caps.minHeight;
    if (outH > caps.maxHeight) outH = caps.maxHeight;
  }

  std::wstring extensionForContentType(const std::string& contentType, const std::string& requestedFormat)
  {
    std::string ct = toLowerAscii(contentType.empty() ? requestedFormat : contentType);
    if (ct.find("application/pdf") != std::string::npos)
    {
      return L".pdf";
    }
    if (ct.find("image/tiff") != std::string::npos)
    {
      return L".tif";
    }
    if (ct.find("image/png") != std::string::npos)
    {
      return L".png";
    }
    return L".jpg";
  }

  void sleepCancellable(int totalMs, const std::shared_ptr<EsclCancelToken>& cancelToken)
  {
    const int step = 50;
    for (int elapsed = 0; elapsed < totalMs; elapsed += step)
    {
      if (cancelToken && cancelToken->isCancelled())
      {
        return;
      }
      ::Sleep((DWORD)(std::min)(step, totalMs - elapsed));
    }
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////
// EsclCancelToken
//////////////////////////////////////////////////////////////////////////////
EsclCancelToken::EsclCancelToken()
  : m_cancelled(false)
  , m_activeHandle(NULL)
{
}

EsclCancelToken::~EsclCancelToken()
{
}

void EsclCancelToken::cancel()
{
  m_cancelled.store(true);
#ifdef _WIN32
  void* handleToClose = NULL;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    handleToClose = m_activeHandle;
    m_activeHandle = NULL;
  }
  if (handleToClose)
  {
    ::WinHttpCloseHandle((HINTERNET)handleToClose);
  }
#endif
}

bool EsclCancelToken::isCancelled() const
{
  return m_cancelled.load();
}

bool EsclCancelToken::setActiveHandle(void* hInternet)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_cancelled.load())
  {
    return false;
  }
  m_activeHandle = hInternet;
  return true;
}

bool EsclCancelToken::clearActiveHandle(void* hInternet)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_activeHandle == hInternet)
  {
    m_activeHandle = NULL;
    return true;
  }
  return false;
}

//////////////////////////////////////////////////////////////////////////////
// CEsclClient settings persistence
//////////////////////////////////////////////////////////////////////////////
void CEsclClient::upsertScanner(EsclUserSettings& settings, const std::string& name, const std::string& url)
{
  std::string cleanUrl = stripTrailingSlashes(trimString(url));
  std::string cleanName = trimString(name);
  if (cleanUrl.empty())
  {
    return;
  }

  std::string lowerUrl = toLowerAscii(cleanUrl);
  for (size_t i = 0; i < settings.scanners.size(); ++i)
  {
    if (toLowerAscii(stripTrailingSlashes(settings.scanners[i].url)) == lowerUrl)
    {
      if (cleanName.empty())
      {
        cleanName = settings.scanners[i].name;
      }
      settings.scanners.erase(settings.scanners.begin() + i);
      break;
    }
  }

  EsclScannerEntry entry;
  entry.name = cleanName;
  entry.url  = cleanUrl;
  settings.scanners.insert(settings.scanners.begin(), entry);

  if (settings.scanners.size() > 10)
  {
    settings.scanners.resize(10);
  }
}

EsclUserSettings CEsclClient::loadSettings()
{
  EsclUserSettings settings;
#ifdef _WIN32
  // 1. Load optional defaults from source.json in the DS directory
  std::wstring dsDir = getDataSourceDirW();
  if (!dsDir.empty())
  {
    std::string raw;
    if (readFileUtf8(dsDir + L"\\source.json", raw))
    {
      try
      {
        json cfg = json::parse(raw);
        if (cfg.is_object())
        {
          if (cfg.contains("escl_scanners") && cfg["escl_scanners"].is_array())
          {
            const json& arr = cfg["escl_scanners"];
            for (size_t i = arr.size(); i > 0; --i)
            {
              const json& item = arr[i - 1];
              if (item.is_string())
              {
                upsertScanner(settings, std::string(), item.get<std::string>());
              }
              else if (item.is_object() && item.contains("url") && item["url"].is_string())
              {
                std::string name = (item.contains("name") && item["name"].is_string())
                  ? item["name"].get<std::string>()
                  : std::string();
                upsertScanner(settings, name, item["url"].get<std::string>());
              }
            }
          }
          if (cfg.contains("escl_url") && cfg["escl_url"].is_string())
          {
            upsertScanner(settings, std::string(), cfg["escl_url"].get<std::string>());
          }
        }
      }
      catch (...)
      {
      }
    }
  }

  // 2. Merge user-saved settings from %LOCALAPPDATA%\VirtualScanner\escl_settings.json
  std::wstring userDir = getUserSettingsDirW();
  if (!userDir.empty())
  {
    std::string raw;
    if (readFileUtf8(userDir + L"\\escl_settings.json", raw))
    {
      try
      {
        json cfg = json::parse(raw);
        if (cfg.is_object())
        {
          if (cfg.contains("last_source") && cfg["last_source"].is_number_integer())
          {
            int src = cfg["last_source"].get<int>();
            if (src >= ESCL_SOURCE_PLATEN && src <= ESCL_SOURCE_ADF_DUPLEX)
            {
              settings.lastSource = src;
            }
          }
          if (cfg.contains("last_paper") && cfg["last_paper"].is_number_integer())
          {
            int paper = cfg["last_paper"].get<int>();
            if (paper >= ESCL_PAPER_A4 && paper <= ESCL_PAPER_MAX_GLASS)
            {
              settings.lastPaper = paper;
            }
          }
          if (cfg.contains("auto_send") && cfg["auto_send"].is_boolean())
          {
            settings.autoSend = cfg["auto_send"].get<bool>();
          }
          if (cfg.contains("scanners") && cfg["scanners"].is_array())
          {
            const json& arr = cfg["scanners"];
            for (size_t i = arr.size(); i > 0; --i)
            {
              const json& item = arr[i - 1];
              if (item.is_object() && item.contains("url") && item["url"].is_string())
              {
                std::string name = (item.contains("name") && item["name"].is_string())
                  ? item["name"].get<std::string>()
                  : std::string();
                upsertScanner(settings, name, item["url"].get<std::string>());
              }
            }
          }
        }
      }
      catch (...)
      {
      }
    }
  }
#endif
  return settings;
}

void CEsclClient::saveSettings(const EsclUserSettings& settings)
{
#ifdef _WIN32
  std::wstring userDir = getUserSettingsDirW();
  if (userDir.empty())
  {
    return;
  }

  try
  {
    json cfg;
    cfg["last_source"] = settings.lastSource;
    cfg["last_paper"]  = settings.lastPaper;
    cfg["auto_send"]   = settings.autoSend;

    json arr = json::array();
    for (size_t i = 0; i < settings.scanners.size(); ++i)
    {
      json item;
      item["name"] = settings.scanners[i].name;
      item["url"]  = settings.scanners[i].url;
      arr.push_back(item);
    }
    cfg["scanners"] = arr;

    std::string serialized = cfg.dump(2);
    writeBytesToFile(userDir + L"\\escl_settings.json", serialized.data(), serialized.size());
  }
  catch (...)
  {
  }
#else
  (void)settings;
#endif
}

//////////////////////////////////////////////////////////////////////////////
// Temp file cleanup
//////////////////////////////////////////////////////////////////////////////
void CEsclClient::cleanupOldTempFiles()
{
#ifdef _WIN32
  std::wstring tempDir = getTempScanDirW();
  if (tempDir.empty())
  {
    return;
  }

  FILETIME nowFt;
  ::GetSystemTimeAsFileTime(&nowFt);
  ULARGE_INTEGER nowUl;
  nowUl.LowPart  = nowFt.dwLowDateTime;
  nowUl.HighPart = nowFt.dwHighDateTime;
  const ULONGLONG kOneDay100ns = 24ULL * 60ULL * 60ULL * 10000000ULL;

  WIN32_FIND_DATAW fd;
  HANDLE hFind = ::FindFirstFileW((tempDir + L"\\escl_*").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
  {
    return;
  }

  do
  {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
      continue;
    }
    ULARGE_INTEGER fileUl;
    fileUl.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
    fileUl.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    if (nowUl.QuadPart > fileUl.QuadPart && (nowUl.QuadPart - fileUl.QuadPart) > kOneDay100ns)
    {
      ::DeleteFileW((tempDir + L"\\" + fd.cFileName).c_str());
    }
  } while (::FindNextFileW(hFind, &fd));

  ::FindClose(hFind);
#endif
}

//////////////////////////////////////////////////////////////////////////////
// eSCL scan workflow
//////////////////////////////////////////////////////////////////////////////
EsclScanResult CEsclClient::scan(
  const EsclScanRequest& request,
  const std::shared_ptr<EsclCancelToken>& cancelToken,
  ProgressCallback onProgress)
{
  EsclScanResult result;
#ifndef _WIN32
  (void)request;
  (void)cancelToken;
  (void)onProgress;
  result.error = "eSCL scanning is only supported on Windows.";
  return result;
#else
  cleanupOldTempFiles();

  std::vector<std::string> candidates = buildCandidateBaseUrls(request.target);
  if (candidates.empty())
  {
    result.error = "Enter a scanner IP address or eSCL URL (e.g. 192.168.1.100).";
    return result;
  }

  // Step 1: Probe ScannerCapabilities to find working base URL & device caps
  std::string baseUrl;
  EsclCapabilities caps;
  std::string lastProbeError;

  for (size_t i = 0; i < candidates.size(); ++i)
  {
    if (cancelToken && cancelToken->isCancelled())
    {
      result.cancelled = true;
      result.error = "Scan cancelled.";
      return result;
    }

    if (onProgress)
    {
      onProgress("Connecting to " + candidates[i] + "...");
    }

    HttpResponse capResp = performHttpRequest(
      "GET",
      candidates[i] + "/ScannerCapabilities",
      std::string(),
      std::string(),
      3500,
      5000,
      cancelToken);

    if (capResp.cancelled)
    {
      result.cancelled = true;
      result.error = "Scan cancelled.";
      return result;
    }

    if (capResp.transportOk && capResp.statusCode == 200 &&
        capResp.body.find("ScannerCapabilities") != std::string::npos)
    {
      baseUrl = candidates[i];
      caps = parseCapabilitiesXml(capResp.body);
      break;
    }

    if (!capResp.error.empty())
    {
      lastProbeError = capResp.error;
    }
    else if (capResp.statusCode != 0)
    {
      std::ostringstream oss;
      oss << "HTTP " << capResp.statusCode;
      lastProbeError = oss.str();
    }
  }

  if (baseUrl.empty())
  {
    result.error = "Could not reach eSCL scanner at \"" + request.target + "\"";
    if (!lastProbeError.empty())
    {
      result.error += " (" + lastProbeError + ")";
    }
    result.error += ".";
    return result;
  }

  result.resolvedUrl  = baseUrl;
  result.makeAndModel = caps.makeAndModel;

  // Step 2: Select input capabilities and validate requested source
  const EsclInputCaps* activeCaps = &caps.platen;
  if (request.source == ESCL_SOURCE_ADF_SIMPLEX)
  {
    if (!caps.adfSimplex.present && !caps.adfDuplex.present)
    {
      result.error = "This scanner does not report an ADF (document feeder). Try Flatbed (Glass).";
      return result;
    }
    activeCaps = caps.adfSimplex.present ? &caps.adfSimplex : &caps.adfDuplex;
  }
  else if (request.source == ESCL_SOURCE_ADF_DUPLEX)
  {
    if (!caps.adfDuplex.present)
    {
      result.error = "This scanner does not report ADF Duplex support. Try ADF (Simplex) or Flatbed.";
      return result;
    }
    activeCaps = &caps.adfDuplex;
  }

  int chosenDpi = pickNearestResolution(activeCaps->resolutions, request.dpi);
  int width = 2480;
  int height = 3508;
  computePaperDimensions(request.paper, *activeCaps, width, height);

  // Use Grayscale8 + JPEG for both Grayscale and B&W (CScanner_FreeImage handles
  // 1-bit thresholding locally, avoiding BlackAndWhite1+JPEG firmware rejections).
  std::string colorMode = "RGB24";
  if (request.pixelType == kTwptRgb)
  {
    colorMode = activeCaps->supportsRgb24 ? "RGB24" : "Grayscale8";
  }
  else
  {
    colorMode = activeCaps->supportsGray8 ? "Grayscale8" : "RGB24";
  }

  std::string docFormat = "image/jpeg";
  if (!activeCaps->supportsJpeg)
  {
    if (activeCaps->supportsPdf)
    {
      docFormat = "application/pdf";
    }
    else if (activeCaps->supportsTiff)
    {
      docFormat = "image/tiff";
    }
  }

  // Step 3: POST /ScanJobs with eSCL ScanSettings XML
  std::ostringstream xml;
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<scan:ScanSettings xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\" "
      << "xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n"
      << "  <pwg:Version>" << (caps.version.empty() ? "2.0" : caps.version) << "</pwg:Version>\n";
  if (activeCaps->supportsIntentDocument)
  {
    xml << "  <scan:Intent>Document</scan:Intent>\n";
  }
  xml << "  <pwg:ScanRegions>\n"
      << "    <pwg:ScanRegion>\n"
      << "      <pwg:Height>" << height << "</pwg:Height>\n"
      << "      <pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>\n"
      << "      <pwg:Width>" << width << "</pwg:Width>\n"
      << "      <pwg:XOffset>0</pwg:XOffset>\n"
      << "      <pwg:YOffset>0</pwg:YOffset>\n"
      << "    </pwg:ScanRegion>\n"
      << "  </pwg:ScanRegions>\n";

  if (request.source == ESCL_SOURCE_PLATEN)
  {
    xml << "  <pwg:InputSource>Platen</pwg:InputSource>\n";
  }
  else
  {
    xml << "  <pwg:InputSource>Feeder</pwg:InputSource>\n"
        << "  <scan:Duplex>" << (request.source == ESCL_SOURCE_ADF_DUPLEX ? "true" : "false") << "</scan:Duplex>\n";
  }

  xml << "  <scan:ColorMode>" << colorMode << "</scan:ColorMode>\n"
      << "  <scan:XResolution>" << chosenDpi << "</scan:XResolution>\n"
      << "  <scan:YResolution>" << chosenDpi << "</scan:YResolution>\n"
      << "  <pwg:DocumentFormat>" << docFormat << "</pwg:DocumentFormat>\n"
      << "</scan:ScanSettings>\n";

  if (onProgress)
  {
    std::string label = caps.makeAndModel.empty() ? baseUrl : caps.makeAndModel;
    onProgress("Starting scan on " + label + "...");
  }

  HttpResponse jobResp = performHttpRequest(
    "POST",
    baseUrl + "/ScanJobs",
    "text/xml; charset=utf-8",
    xml.str(),
    5000,
    30000,
    cancelToken);

  if (jobResp.cancelled)
  {
    result.cancelled = true;
    result.error = "Scan cancelled.";
    return result;
  }

  if (!jobResp.transportOk)
  {
    result.error = "Failed to start scan job: " + jobResp.error;
    return result;
  }

  if (jobResp.statusCode != 201 && jobResp.statusCode != 200)
  {
    std::ostringstream err;
    if (jobResp.statusCode == 503)
    {
      err << "Scanner is busy (HTTP 503). Wait a moment and try again.";
    }
    else if (jobResp.statusCode == 409)
    {
      err << "Scanner rejected the scan request (HTTP 409). Check paper in feeder or try Flatbed.";
    }
    else
    {
      err << "Scanner rejected scan job (HTTP " << jobResp.statusCode << ").";
    }
    result.error = err.str();
    return result;
  }

  // Resolve Location header to full job URL
  std::string jobUrl;
  std::string loc = stripTrailingSlashes(trimString(jobResp.location));
  if (loc.empty())
  {
    result.error = "Scanner did not return a ScanJobs Location header.";
    return result;
  }
  else if (startsWithNoCase(loc, "http://") || startsWithNoCase(loc, "https://"))
  {
    jobUrl = loc;
  }
  else if (loc[0] == '/')
  {
    ParsedUrl baseParsed = parseUrl(baseUrl);
    jobUrl = buildOrigin(baseParsed) + loc;
  }
  else
  {
    jobUrl = baseUrl + "/ScanJobs/" + loc;
  }

  // Step 4: Download page(s) via GET <jobUrl>/NextDocument
  std::wstring tempDir = getTempScanDirW();
  if (tempDir.empty())
  {
    performHttpRequest("DELETE", jobUrl, std::string(), std::string(), 2000, 3000, std::shared_ptr<EsclCancelToken>());
    result.error = "Could not create temporary folder for scanned pages.";
    return result;
  }

  DWORD tick = ::GetTickCount();
  DWORD pid  = ::GetCurrentProcessId();
  int maxPages = (request.source == ESCL_SOURCE_PLATEN) ? 1 : 100;

  for (int pageNum = 1; pageNum <= maxPages; ++pageNum)
  {
    if (cancelToken && cancelToken->isCancelled())
    {
      result.cancelled = true;
      result.error = "Scan cancelled.";
      break;
    }

    if (onProgress)
    {
      std::ostringstream msg;
      msg << "Scanning page " << pageNum << "...";
      onProgress(msg.str());
    }

    HttpResponse docResp;
    const int maxBusyRetries = 60; // up to ~30s of HTTP 503 warm-up retries
    for (int attempt = 0; attempt < maxBusyRetries; ++attempt)
    {
      docResp = performHttpRequest(
        "GET",
        jobUrl + "/NextDocument",
        std::string(),
        std::string(),
        5000,
        90000,
        cancelToken);

      if (docResp.cancelled || !docResp.transportOk || docResp.statusCode != 503)
      {
        break;
      }
      sleepCancellable(500, cancelToken);
    }

    if (docResp.cancelled)
    {
      result.cancelled = true;
      result.error = "Scan cancelled.";
      break;
    }

    if (!docResp.transportOk)
    {
      if (result.savedFiles.empty())
      {
        result.error = "Failed to download scanned image: " + docResp.error;
      }
      break;
    }

    if (docResp.statusCode == 404 || docResp.statusCode == 410)
    {
      // Standard eSCL end-of-job signal (no more pages)
      break;
    }

    if (docResp.statusCode != 200)
    {
      if (result.savedFiles.empty())
      {
        std::ostringstream err;
        err << "Scanner returned HTTP " << docResp.statusCode << " while reading page " << pageNum << ".";
        result.error = err.str();
      }
      break;
    }

    if (docResp.body.empty())
    {
      if (result.savedFiles.empty())
      {
        result.error = "Scanner returned an empty image.";
      }
      break;
    }

    std::wstring ext = extensionForContentType(docResp.contentType, docFormat);
    std::wostringstream fileName;
    fileName << tempDir << L"\\escl_" << tick << L"_" << pid << L"_p" << pageNum << ext;
    std::wstring fullPath = fileName.str();

    if (!writeBytesToFile(fullPath, docResp.body.data(), docResp.body.size()))
    {
      result.error = "Failed to save scanned image to temporary folder.";
      break;
    }

    result.savedFiles.push_back(fullPath);

    if (request.source == ESCL_SOURCE_PLATEN)
    {
      break;
    }
  }

  // Step 5: Best-effort DELETE <jobUrl> so the copier releases the scan session cleanly
  performHttpRequest("DELETE", jobUrl, std::string(), std::string(), 2000, 3000, std::shared_ptr<EsclCancelToken>());

  if (result.cancelled)
  {
    for (size_t i = 0; i < result.savedFiles.size(); ++i)
    {
      ::DeleteFileW(result.savedFiles[i].c_str());
    }
    result.savedFiles.clear();
    return result;
  }

  if (result.savedFiles.empty())
  {
    if (result.error.empty())
    {
      result.error = "No pages were scanned. Check that a document is placed in the scanner.";
    }
    return result;
  }

  result.ok = true;
  return result;
#endif
}
