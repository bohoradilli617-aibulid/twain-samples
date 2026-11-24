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
* @file CScanner_FreeImage.h
* Defines a virtual TWAIN scanner backed by FreeImage and PDFium.
*/
#ifndef __CSCANNER_H__
#define __CSCANNER_H__

#include "Common.h"
#include "FreeImage.h"
#include "twain.h"
#include <string>
#include <vector>

#define SFI_PAPERSOURCE_ADF 0
#define SFI_PAPERSOURCE_FB  1

struct SFreeImage
{
  WORD              m_nPaperSource;           /**< the current paper source ADF or Flatbed*/
  bool              m_bDuplex;                /**< True to use duplex false for simplex, ignored if flatbed*/
  WORD              m_nPixelType;             /**< type of pixels to transfer image as */
  int               m_nRight;                 /**< frame right edge */
  int               m_nBottom;                /**< frame bottom edge */
  int               m_nLeft;                  /**< frame left edge */
  int               m_nTop;                   /**< frame top edge */
  float             m_fXResolution;           /**< horizontal resolution */
  float             m_fYResolution;           /**< vertical resolution */
  float             m_fGamma;                 /**< Gamma */
  float             m_fContrast;              /**< Contrast */
  float             m_fBrightness;            /**< Brightness */
  float             m_fThreshold;             /**< Threshold */
};

struct ScanPageItem
{
  std::wstring filePath;
  int          pageIndex;
  bool         isPdf;
  bool         isMultiTiff;

  ScanPageItem() : pageIndex(0), isPdf(false), isMultiTiff(false) {}
  ScanPageItem(const std::wstring& path, int page, bool pdf, bool multiTiff)
    : filePath(path), pageIndex(page), isPdf(pdf), isMultiTiff(multiTiff) {}
};

class CScanner_FreeImage : public SFreeImage
{
public:
  CScanner_FreeImage();
  ~CScanner_FreeImage();

  bool resetScanner();
  void loadSourceConfig();

  bool getScanStrip(BYTE *pTransferBuffer, DWORD dwRead, DWORD &dwReceived);

  void Lock(void)   { m_bReadOnly = true; }
  void Unlock(void) { m_bReadOnly = false; }

  bool acquireImage();

  SFreeImage getSetting() const;
  void setSetting(SFreeImage settings);

  bool isFeederLoaded();
  bool getDeviceOnline() const;

  short GetMaxPagesInADF(void) { return m_nMaxDocCount; }
  void  SetMaxPagesInADF(short nVal) { m_nMaxDocCount = nVal; }

  void setSelectedPages(const std::vector<ScanPageItem>& pages);
  void clearSelectedPages();
  bool hasSelectedPages() const { return m_bUseSelectedPages && !m_selectedPages.empty(); }
  int  getSelectedPageCount() const { return (int)m_selectedPages.size(); }

  bool getForceUI() const { return m_bForceUI; }
  bool isForceUIEnabled();

  static int getFilePageCount(const std::wstring& filePath, bool& outIsPdf, bool& outIsMultiTiff);

protected:
  short getDocumentCount() const;
  bool preScanPrep();

public:
  static bool ms_blShowConsoleMessages;

protected:
  FIBITMAP         *m_pDIB;                   /**< Pointer to current scanned image */
  WORD              m_nScanLine;              /**< Current scan line of image in FreeImage */
  bool              m_bReadOnly;              /**< current mode */
  DWORD             m_nDestBytesPerRow;       /**< number of bytes needed for a row of data */
  DWORD             m_nRowOffset;             /**< offset of the first byte on every row*/
  short             m_nDocCount;              /**< number of documents waiting to transfer */
  short             m_nMaxDocCount;           /**< Max number of documents waiting to transfer */
  WORD              m_nSourceWidth;           /**< Width of image in FreeImage */
  WORD              m_nSourceHeight;          /**< Height of image in FreeImage */
  char              m_szSourceImagePath[PATH_MAX];  /**< fallback image used with FreeImage */

  std::vector<std::string>  m_folderImages;   /**< optional fallback image list from source.json folder */
  size_t                    m_nFolderPageIdx; /**< current index inside m_folderImages */
  int                       m_nJobPageIndex;  /**< 0-based page index inside current scan job */
  std::vector<ScanPageItem> m_selectedPages;  /**< user-selected page queue (PDF/images) */
  bool                      m_bUseSelectedPages;   /**< true when scanning from user-selected queue */
  bool                      m_bCurrentPageFromPdf; /**< true when current m_pDIB was rendered by PDFium */
  bool                      m_bForceUI;            /**< true when source.json sets "force_ui": true */
};

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message);

#endif // __CSCANNER_H__
