/***************************************************************************
* Copyright (c) 2007 TWAIN Working Group:
*   Adobe Systems Incorporated, AnyDoc Software Inc., Eastman Kodak Company,
*   Fujitsu Computer Products of America, JFL Peripheral Solutions Inc.,
*   Ricoh Corporation, and Xerox Corporation.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the TWAIN Working Group nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY TWAIN Working Group ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL TWAIN Working Group BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
***************************************************************************/

/**
* @file CScanner_FreeImage.cpp
* Virtual TWAIN scanner implementation using FreeImage and PDFium.
*/

#include "CScanner_FreeImage.h"
#include "CPdfRenderer.h"
#include "DSMInterface.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef TWH_CMP_MSC
#include <io.h>
extern HINSTANCE g_hinstance;
#elif __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#include "CoreFoundation/CFBundle.h"
#include "CoreFoundation/CFURL.h"
#include <Carbon/Carbon.h>
#else
#include <sys/io.h>
#endif

#ifdef TWNDS_OS_LINUX
  #define kTWAIN_DS_DIR "/usr/local/lib/twain/sample2"
#endif

#define kGETENV_XFERCOUNT "CAP_XFERCOUNT"

using namespace std;
using json = nlohmann::json;

namespace
{
#ifdef _WIN32
  std::string getDataSourceDir()
  {
    char szDir[PATH_MAX] = {0};
    DWORD len = ::GetModuleFileNameA((HMODULE)g_hinstance, szDir, PATH_MAX - 1);
    if (len == 0)
    {
      return std::string();
    }
    std::string full(szDir, len);
    size_t pos = full.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string() : full.substr(0, pos);
  }

  json readSourceJson()
  {
    std::string dsDir = getDataSourceDir();
    if (dsDir.empty())
    {
      return json();
    }
    std::string cfgPath = dsDir + "\\source.json";
    if (!FILE_EXISTS(cfgPath.c_str()))
    {
      return json();
    }
    try
    {
      std::ifstream ifs(cfgPath.c_str());
      if (ifs.is_open())
      {
        json parsed;
        ifs >> parsed;
        return parsed;
      }
    }
    catch (...)
    {
    }
    return json();
  }

  bool isSupportedFallbackImage(const std::string& name, DWORD attrs)
  {
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
    {
      return false;
    }
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
    {
      return false;
    }
    std::string ext = name.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
    {
      ext[i] = (char)std::tolower((unsigned char)ext[i]);
    }
    return (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff");
  }

  unsigned DLL_CALLCONV fiWideRead(void* buffer, unsigned size, unsigned count, fi_handle handle)
  {
    return (unsigned)fread(buffer, size, count, (FILE*)handle);
  }

  unsigned DLL_CALLCONV fiWideWrite(void* buffer, unsigned size, unsigned count, fi_handle handle)
  {
    return (unsigned)fwrite(buffer, size, count, (FILE*)handle);
  }

  int DLL_CALLCONV fiWideSeek(fi_handle handle, long offset, int origin)
  {
    return fseek((FILE*)handle, offset, origin);
  }

  long DLL_CALLCONV fiWideTell(fi_handle handle)
  {
    return ftell((FILE*)handle);
  }

  void initWideFreeImageIO(FreeImageIO& io)
  {
    io.read_proc  = fiWideRead;
    io.write_proc = fiWideWrite;
    io.seek_proc  = fiWideSeek;
    io.tell_proc  = fiWideTell;
  }

  FIBITMAP* loadMultiTiffPageU(const std::wstring& filePath, int pageIndex)
  {
    FILE* fp = NULL;
    if (_wfopen_s(&fp, filePath.c_str(), L"rb") != 0 || !fp)
    {
      return NULL;
    }
    FreeImageIO io;
    initWideFreeImageIO(io);
    FIMULTIBITMAP* multi = FreeImage_OpenMultiBitmapFromHandle(FIF_TIFF, &io, (fi_handle)fp, 0);
    FIBITMAP* result = NULL;
    if (multi)
    {
      int count = FreeImage_GetPageCount(multi);
      if (pageIndex >= 0 && pageIndex < count)
      {
        FIBITMAP* page = FreeImage_LockPage(multi, pageIndex);
        if (page)
        {
          result = FreeImage_Clone(page);
          FreeImage_UnlockPage(multi, page, FALSE);
        }
      }
      FreeImage_CloseMultiBitmap(multi, 0);
    }
    fclose(fp);
    return result;
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message)
{
  if (CScanner_FreeImage::ms_blShowConsoleMessages)
  {
    cout << "\n*** ";
    cout << FreeImage_GetFormatFromFIF(fif) << " Format" << endl;
    cout << message << " ***" << endl;
  }
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::ms_blShowConsoleMessages = false;

CScanner_FreeImage::CScanner_FreeImage() :
  m_pDIB(0),
  m_nScanLine(0),
  m_bReadOnly(false),
  m_nDestBytesPerRow(0),
  m_nRowOffset(0),
  m_nDocCount(0),
  m_nMaxDocCount(1),
  m_nSourceWidth(0),
  m_nSourceHeight(0),
  m_nFolderPageIdx(0),
  m_nJobPageIndex(0),
  m_bUseSelectedPages(false),
  m_bCurrentPageFromPdf(false),
  m_bForceUI(false)
{
  memset(m_szSourceImagePath, 0, PATH_MAX);

#ifdef TWH_CMP_MSC
  char szEnv[8];
  CScanner_FreeImage::ms_blShowConsoleMessages = (::GetEnvironmentVariableA("TWAIN_SHOWCONSOLEMESSAGES", szEnv, sizeof(szEnv)) != 0);

  std::string dsDir = getDataSourceDir();
  if (!dsDir.empty())
  {
    SSNPRINTF(m_szSourceImagePath, sizeof(m_szSourceImagePath), PATH_MAX, "%s%cTWAIN_logo.png", dsDir.c_str(), PATH_SEPERATOR);
  }
#elif defined(TWNDS_OS_APPLE)
  CScanner_FreeImage::ms_blShowConsoleMessages = (getenv("TWAIN_SHOWCONSOLEMESSAGES") != 0);
  CFURLRef    fileURL = NULL;
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.yourcompany.TWAINDS-Mac"));
  fileURL = CFBundleCopyResourceURL(bundle, CFSTR("TWAIN_logo"), CFSTR("png"), NULL);
  if (fileURL)
  {
    FSRef fsRef;
    if (CFURLGetFSRef(fileURL, &fsRef))
    {
      FSRefMakePath(&fsRef, (UInt8*)m_szSourceImagePath, PATH_MAX);
    }
    CFRelease(fileURL);
  }
#else
  CScanner_FreeImage::ms_blShowConsoleMessages = (getenv("TWAIN_SHOWCONSOLEMESSAGES") != 0);
  SSNPRINTF(m_szSourceImagePath, sizeof(m_szSourceImagePath), PATH_MAX, "%s%cTWAIN_logo.png", kTWAIN_DS_DIR, PATH_SEPERATOR);
#endif

  resetScanner();

  FreeImage_Initialise();
  FreeImage_SetOutputMessage(FreeImageErrorHandler);
}

//////////////////////////////////////////////////////////////////////////////
CScanner_FreeImage::~CScanner_FreeImage()
{
  if (0 != m_pDIB)
  {
    FreeImage_Unload(m_pDIB);
  }
  FreeImage_DeInitialise();
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::resetScanner()
{
  Unlock();

  m_nScanLine        = 0;
  m_nDestBytesPerRow = 0;
  m_nRowOffset       = 0;

  m_nDocCount = m_nMaxDocCount = getDocumentCount();
  m_nPixelType    = TWPT_RGB;
  m_nPaperSource  = SFI_PAPERSOURCE_ADF;
  m_bDuplex       = false;
  m_nRight        = 0;
  m_nBottom       = 0;
  m_nLeft         = 0;
  m_nTop          = 0;
  m_fXResolution  = 200.0;
  m_fYResolution  = 200.0;

  m_selectedPages.clear();
  m_bUseSelectedPages   = false;
  m_bCurrentPageFromPdf = false;
  m_bForceUI            = false;
  m_nJobPageIndex       = 0;

  if (0 != m_pDIB)
  {
    FreeImage_Unload(m_pDIB);
    m_pDIB = 0;
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::isForceUIEnabled()
{
#ifdef _WIN32
  json cfg = readSourceJson();
  if (cfg.is_object() && cfg.contains("force_ui") && cfg["force_ui"].is_boolean())
  {
    m_bForceUI = cfg["force_ui"].get<bool>();
  }
  else
  {
    m_bForceUI = false;
  }
#endif
  return m_bForceUI;
}

//////////////////////////////////////////////////////////////////////////////
void CScanner_FreeImage::loadSourceConfig()
{
  m_folderImages.clear();
  m_nFolderPageIdx = 0;
  m_nJobPageIndex  = 0;
  m_bForceUI       = false;

#ifdef _WIN32
  json cfg = readSourceJson();
  std::string folder;
  if (cfg.is_object())
  {
    if (cfg.contains("force_ui") && cfg["force_ui"].is_boolean())
    {
      m_bForceUI = cfg["force_ui"].get<bool>();
    }
    if (cfg.contains("folder") && cfg["folder"].is_string())
    {
      folder = cfg["folder"].get<std::string>();
    }
  }

  if (folder.empty())
  {
    return;
  }

  DWORD attrs = ::GetFileAttributesA(folder.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
  {
    return;
  }

  WIN32_FIND_DATAA fd;
  HANDLE hFind = ::FindFirstFileA((folder + "\\*").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
  {
    return;
  }

  do
  {
    if (isSupportedFallbackImage(fd.cFileName, fd.dwFileAttributes))
    {
      m_folderImages.push_back(folder + "\\" + fd.cFileName);
    }
  } while (::FindNextFileA(hFind, &fd));

  ::FindClose(hFind);

  if (!m_folderImages.empty())
  {
    std::sort(m_folderImages.begin(), m_folderImages.end());
    m_nDocCount = m_nMaxDocCount = (short)m_folderImages.size();
    memset(m_szSourceImagePath, 0, PATH_MAX);
    SSNPRINTF(m_szSourceImagePath, sizeof(m_szSourceImagePath), PATH_MAX, "%s", m_folderImages[0].c_str());
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////
void CScanner_FreeImage::setSelectedPages(const std::vector<ScanPageItem>& pages)
{
  m_selectedPages = pages;
  m_nJobPageIndex = 0;
  m_bUseSelectedPages = !m_selectedPages.empty();
  if (m_bUseSelectedPages)
  {
    m_nDocCount = m_nMaxDocCount = (short)m_selectedPages.size();
  }
}

//////////////////////////////////////////////////////////////////////////////
void CScanner_FreeImage::clearSelectedPages()
{
  m_selectedPages.clear();
  m_nJobPageIndex = 0;
  m_bUseSelectedPages = false;
  m_bCurrentPageFromPdf = false;
}

//////////////////////////////////////////////////////////////////////////////
int CScanner_FreeImage::getFilePageCount(const std::wstring& filePath, bool& outIsPdf, bool& outIsMultiTiff)
{
  outIsPdf = false;
  outIsMultiTiff = false;

  if (CPdfRenderer::isPdfPath(filePath))
  {
    outIsPdf = true;
    return CPdfRenderer::instance().getPageCount(filePath);
  }

#ifdef _WIN32
  if (CPdfRenderer::isTiffPath(filePath))
  {
    FILE* fp = NULL;
    if (_wfopen_s(&fp, filePath.c_str(), L"rb") == 0 && fp)
    {
      FreeImageIO io;
      initWideFreeImageIO(io);
      FIMULTIBITMAP* multi = FreeImage_OpenMultiBitmapFromHandle(FIF_TIFF, &io, (fi_handle)fp, 0);
      int count = 0;
      if (multi)
      {
        count = FreeImage_GetPageCount(multi);
        FreeImage_CloseMultiBitmap(multi, 0);
      }
      fclose(fp);
      if (count > 1)
      {
        outIsMultiTiff = true;
        return count;
      }
      if (count == 1)
      {
        return 1;
      }
    }
  }

  FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeU(filePath.c_str(), 0);
  if (fif == FIF_UNKNOWN)
  {
    fif = FreeImage_GetFIFFromFilenameU(filePath.c_str());
  }
  if (fif != FIF_UNKNOWN && FreeImage_FIFSupportsReading(fif))
  {
    return 1;
  }
#endif

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
SFreeImage CScanner_FreeImage::getSetting() const
{
  return (SFreeImage)(*this);
}

//////////////////////////////////////////////////////////////////////////////
void CScanner_FreeImage::setSetting(SFreeImage settings)
{
  m_nPaperSource = settings.m_nPaperSource;
  m_bDuplex      = settings.m_bDuplex;
  m_nPixelType   = settings.m_nPixelType;
  m_nRight       = settings.m_nRight;
  m_nBottom      = settings.m_nBottom;
  m_nLeft        = settings.m_nLeft;
  m_nTop         = settings.m_nTop;
  m_fXResolution = settings.m_fXResolution;
  m_fYResolution = settings.m_fYResolution;
  m_fGamma       = settings.m_fGamma;
  m_fContrast    = settings.m_fContrast;
  m_fBrightness  = settings.m_fBrightness;
  m_fThreshold   = settings.m_fThreshold;

  if (m_bUseSelectedPages && !m_selectedPages.empty())
  {
    m_nDocCount = m_nMaxDocCount = (short)m_selectedPages.size();
  }
  else if (m_nPaperSource == SFI_PAPERSOURCE_ADF)
  {
    m_nDocCount = m_nMaxDocCount;
  }
  else
  {
    m_nDocCount = 1;
  }
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::acquireImage()
{
  if (0 != m_pDIB)
  {
    FreeImage_Unload(m_pDIB);
    m_pDIB = 0;
  }

  m_bCurrentPageFromPdf = false;

  if (m_bUseSelectedPages && !m_selectedPages.empty())
  {
    if (m_nJobPageIndex < 0 || (size_t)m_nJobPageIndex >= m_selectedPages.size())
    {
      return false;
    }

    const ScanPageItem& item = m_selectedPages[(size_t)m_nJobPageIndex++];
    if (item.isPdf)
    {
      m_pDIB = CPdfRenderer::instance().renderPageToDIB(item.filePath, item.pageIndex, m_fXResolution, m_fYResolution);
      m_bCurrentPageFromPdf = (0 != m_pDIB);
    }
#ifdef _WIN32
    else if (item.isMultiTiff)
    {
      m_pDIB = loadMultiTiffPageU(item.filePath, item.pageIndex);
    }
    else
    {
      FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeU(item.filePath.c_str(), 0);
      if (fif == FIF_UNKNOWN)
      {
        fif = FreeImage_GetFIFFromFilenameU(item.filePath.c_str());
      }
      if (fif != FIF_UNKNOWN)
      {
        m_pDIB = FreeImage_LoadU(fif, item.filePath.c_str(), 0);
      }
    }
#endif

    if (0 == m_pDIB)
    {
      cout << "ds: Failed - could not acquire selected page" << endl;
      return false;
    }

    m_nDocCount--;
    return preScanPrep();
  }

  if (!m_folderImages.empty())
  {
    const std::string& nextImg = m_folderImages[m_nFolderPageIdx % m_folderImages.size()];
    m_nFolderPageIdx++;
    memset(m_szSourceImagePath, 0, PATH_MAX);
    SSNPRINTF(m_szSourceImagePath, sizeof(m_szSourceImagePath), PATH_MAX, "%s", nextImg.c_str());
  }

  if (FALSE == FILE_EXISTS(m_szSourceImagePath))
  {
    cerr << "ds: Could not find required file: " << m_szSourceImagePath << endl;
    return false;
  }

  FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(m_szSourceImagePath);
  if (fif == FIF_UNKNOWN)
  {
    fif = FreeImage_GetFIFFromFilename(m_szSourceImagePath);
  }
  m_pDIB = FreeImage_Load(fif, m_szSourceImagePath);

  if (0 == m_pDIB)
  {
    cout << "ds: Failed - could not acquire image" << endl;
    return false;
  }

  m_nDocCount--;
  return preScanPrep();
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::preScanPrep()
{
  // Normalize to 24-bit BGR (composite 32-bit RGBA images onto white background).
  unsigned int bpp = FreeImage_GetBPP(m_pDIB);
  if (bpp == 32)
  {
    RGBQUAD white = { 255, 255, 255, 0 };
    FIBITMAP* pComp = FreeImage_Composite(m_pDIB, FALSE, &white);
    if (pComp)
    {
      FreeImage_Unload(m_pDIB);
      m_pDIB = pComp;
    }
    FIBITMAP* pDib24 = FreeImage_ConvertTo24Bits(m_pDIB);
    if (!pDib24)
    {
      return false;
    }
    FreeImage_Unload(m_pDIB);
    m_pDIB = pDib24;
  }
  else if (bpp != 24)
  {
    FIBITMAP *pDib24 = FreeImage_ConvertTo24Bits(m_pDIB);
    if (!pDib24)
    {
      return false;
    }
    FreeImage_Unload(m_pDIB);
    m_pDIB = pDib24;
  }

  FIBITMAP *pDib = 0;

  if (m_nPixelType != TWPT_BW)
  {
    if (m_fBrightness != 0)
    {
      FreeImage_AdjustBrightness(m_pDIB, m_fBrightness / 10);
    }
    if (m_fContrast != 0)
    {
      FreeImage_AdjustContrast(m_pDIB, m_fContrast / 10);
    }
    if (m_fGamma != 1.0)
    {
      FreeImage_AdjustGamma(m_pDIB, m_fGamma);
    }
  }

  unsigned int srcW = FreeImage_GetWidth(m_pDIB);
  unsigned int srcH = FreeImage_GetHeight(m_pDIB);
  m_nSourceWidth   = (WORD)srcW;
  m_nSourceHeight  = (WORD)srcH;

  // PDF pages are already rendered by PDFium at exact (m_fXResolution, m_fYResolution).
  // For raster images, safely rescale only when valid DPI metadata is present.
  if (!m_bCurrentPageFromPdf)
  {
    unsigned int resX = FreeImage_GetDotsPerMeterX(m_pDIB);
    unsigned int resY = FreeImage_GetDotsPerMeterY(m_pDIB);
    float srcDpiX = (float)resX / 39.3700787f;
    float srcDpiY = (float)resY / 39.3700787f;

    if (srcDpiX >= 50.0f && srcDpiX <= 2400.0f && srcDpiY >= 50.0f && srcDpiY <= 2400.0f &&
        (std::fabs(srcDpiX - m_fXResolution) > 1.0f || std::fabs(srcDpiY - m_fYResolution) > 1.0f))
    {
      int newWidth  = (int)std::floor((double)srcW * ((double)m_fXResolution / (double)srcDpiX) + 0.5);
      int newHeight = (int)std::floor((double)srcH * ((double)m_fYResolution / (double)srcDpiY) + 0.5);

      if (newWidth < 1) newWidth = 1;
      if (newHeight < 1) newHeight = 1;
      if (newWidth > 12000) newWidth = 12000;
      if (newHeight > 12000) newHeight = 12000;

      if (CScanner_FreeImage::ms_blShowConsoleMessages)
      {
        cout << "ds: rescaling... to " << newWidth << " x " << newHeight << endl;
      }
      pDib = FreeImage_Rescale(m_pDIB, newWidth, newHeight, FILTER_BILINEAR);
      if (0 == pDib)
      {
        return false;
      }
      FreeImage_Unload(m_pDIB);
      m_pDIB = pDib;
    }
  }

  if (m_bUseSelectedPages || m_nRight <= 0 || m_nBottom <= 0)
  {
    m_nRight  = m_nSourceWidth  = (WORD)FreeImage_GetWidth(m_pDIB);
    m_nBottom = m_nSourceHeight = (WORD)FreeImage_GetHeight(m_pDIB);
    m_nLeft = 0;
    m_nTop = 0;
  }
  else
  {
    m_nSourceWidth  = (WORD)FreeImage_GetWidth(m_pDIB);
    m_nSourceHeight = (WORD)FreeImage_GetHeight(m_pDIB);
  }

  FreeImage_SetDotsPerMeterX(m_pDIB, (unsigned)(m_fXResolution * 39.3700787f + 0.5f));
  FreeImage_SetDotsPerMeterY(m_pDIB, (unsigned)(m_fYResolution * 39.3700787f + 0.5f));

  if (m_nPixelType != TWPT_RGB)
  {
    switch (m_nPixelType)
    {
      case TWPT_BW:
        pDib = FreeImage_Threshold(m_pDIB, (BYTE)m_fThreshold);
        break;
      case TWPT_GRAY:
        pDib = FreeImage_ConvertTo8Bits(m_pDIB);
        break;
      case TWPT_RGB:
        pDib = FreeImage_ConvertTo24Bits(m_pDIB);
        break;
    }

    if (0 == pDib)
    {
      return false;
    }
    FreeImage_Unload(m_pDIB);
    m_pDIB = pDib;
  }

  switch (m_nPixelType)
  {
    case TWPT_BW:
      m_nDestBytesPerRow = BYTES_PERLINE(m_nRight - m_nLeft, 1);
      m_nRowOffset = BYTES_PERLINE(m_nLeft, 1);
      break;
    case TWPT_GRAY:
      m_nDestBytesPerRow = BYTES_PERLINE(m_nRight - m_nLeft, 8);
      m_nRowOffset = BYTES_PERLINE(m_nLeft, 8);
      break;
    case TWPT_RGB:
      m_nDestBytesPerRow = BYTES_PERLINE(m_nRight - m_nLeft, 24);
      m_nRowOffset = BYTES_PERLINE(m_nLeft, 24);
      break;
  }

  m_nScanLine = m_nTop;
  return true;
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::getScanStrip(BYTE *pTransferBuffer, DWORD dwRead, DWORD &dwReceived)
{
  dwReceived = 0;

  if (NULL == pTransferBuffer || dwRead < m_nDestBytesPerRow)
  {
    return false;
  }

  BYTE *pBits = NULL;
  WORD  nRow  = 0;
  WORD  nMaxRows = (WORD)(dwRead / m_nDestBytesPerRow);

  if (m_nScanLine < MIN(m_nSourceHeight, m_nBottom))
  {
    for (nRow = 0; nRow < nMaxRows; nRow++)
    {
      pBits = (BYTE*)FreeImage_GetScanLine(m_pDIB, m_nSourceHeight - m_nScanLine - 1);
      memcpy(pTransferBuffer, pBits + m_nRowOffset, MIN(m_nDestBytesPerRow, FreeImage_GetLine(m_pDIB)));

      if (m_nDestBytesPerRow > FreeImage_GetLine(m_pDIB))
      {
        memset(pTransferBuffer + FreeImage_GetLine(m_pDIB), 0, m_nDestBytesPerRow - FreeImage_GetLine(m_pDIB));
      }

      pTransferBuffer += m_nDestBytesPerRow;
      m_nScanLine++;
      dwReceived += m_nDestBytesPerRow;

      if (m_nScanLine >= m_nSourceHeight || m_nScanLine >= m_nBottom)
      {
        break;
      }
    }
  }

  if (m_nBottom > m_nScanLine)
  {
    nMaxRows = (WORD)((dwRead - dwReceived) / m_nDestBytesPerRow);
    memset(pTransferBuffer, 0, m_nDestBytesPerRow * nMaxRows);
    m_nScanLine += nMaxRows;
    dwReceived += m_nDestBytesPerRow * nMaxRows;
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
short CScanner_FreeImage::getDocumentCount() const
{
  int nCount = 2;
  char szCount[10];
  memset(szCount, 0, sizeof(szCount));

  if (0 != SGETENV(szCount, sizeof(szCount), kGETENV_XFERCOUNT))
  {
    nCount = atoi(szCount);
    if (nCount <= 0)
    {
      srand(int(time(0)));
      nCount = rand() % 15;
    }
  }

  return (short)nCount;
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::isFeederLoaded()
{
  bool rtn = true;
  if (m_nDocCount <= 0)
  {
    rtn = false;
    m_nDocCount = m_nMaxDocCount;
  }
  return rtn;
}

//////////////////////////////////////////////////////////////////////////////
bool CScanner_FreeImage::getDeviceOnline() const
{
  return true;
}
