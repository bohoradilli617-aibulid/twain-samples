#ifndef __CPDF_RENDERER_H__
#define __CPDF_RENDERER_H__

#include "Common.h"
#include "FreeImage.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Dynamically loads 32-bit pdfium.dll from the same directory as the TWAIN DS
 * and renders PDF pages on demand into 24-bit FreeImage DIBs at the requested DPI.
 */
class CPdfRenderer
{
public:
  static CPdfRenderer& instance();

  /**
   * Ensures pdfium.dll is loaded and initialized.
   * Returns true if PDF rendering is available.
   */
  bool ensureLoaded();

  /**
   * Returns the number of pages in a PDF file, or 0 on error / if PDFium is unavailable.
   */
  int getPageCount(const std::wstring& filePath);

  /**
   * Renders a 0-based page index of the given PDF file at (dpiX, dpiY) into a
   * newly allocated 24-bit FreeImage FIBITMAP (caller owns and must FreeImage_Unload).
   * Returns nullptr on failure.
   */
  FIBITMAP* renderPageToDIB(const std::wstring& filePath, int pageIndex, float dpiX, float dpiY);

  /**
   * Utility: returns true if filePath ends with ".pdf" (case-insensitive).
   */
  static bool isPdfPath(const std::wstring& filePath);

  /**
   * Utility: returns true if filePath ends with ".tif" or ".tiff" (case-insensitive).
   */
  static bool isTiffPath(const std::wstring& filePath);

private:
  CPdfRenderer();
  ~CPdfRenderer();
  CPdfRenderer(const CPdfRenderer&);
  CPdfRenderer& operator=(const CPdfRenderer&);

#ifdef _WIN32
  static std::string wideToUtf8(const std::wstring& wide);
  static std::wstring getDsDirectory();

  typedef void* FPDF_DOCUMENT;
  typedef void* FPDF_PAGE;
  typedef void* FPDF_BITMAP;
  typedef const char* FPDF_STRING;
  typedef const char* FPDF_BYTESTRING;
  typedef unsigned long FPDF_DWORD;
  typedef int FPDF_BOOL;

  // bblanchon/pdfium-binaries win-x86 exports __cdecl symbols
  typedef void          (__cdecl *PFN_FPDF_InitLibrary)();
  typedef void          (__cdecl *PFN_FPDF_DestroyLibrary)();
  typedef FPDF_DOCUMENT (__cdecl *PFN_FPDF_LoadDocument)(FPDF_STRING file_path, FPDF_BYTESTRING password);
  typedef void          (__cdecl *PFN_FPDF_CloseDocument)(FPDF_DOCUMENT document);
  typedef int           (__cdecl *PFN_FPDF_GetPageCount)(FPDF_DOCUMENT document);
  typedef FPDF_PAGE     (__cdecl *PFN_FPDF_LoadPage)(FPDF_DOCUMENT document, int page_index);
  typedef void          (__cdecl *PFN_FPDF_ClosePage)(FPDF_PAGE page);
  typedef double        (__cdecl *PFN_FPDF_GetPageWidth)(FPDF_PAGE page);
  typedef double        (__cdecl *PFN_FPDF_GetPageHeight)(FPDF_PAGE page);
  typedef FPDF_BITMAP   (__cdecl *PFN_FPDFBitmap_CreateEx)(int width, int height, int format, void* first_scan, int stride);
  typedef FPDF_BOOL     (__cdecl *PFN_FPDFBitmap_FillRect)(FPDF_BITMAP bitmap, int left, int top, int width, int height, FPDF_DWORD color);
  typedef void          (__cdecl *PFN_FPDF_RenderPageBitmap)(FPDF_BITMAP bitmap, FPDF_PAGE page, int start_x, int start_y, int size_x, int size_y, int rotate, int flags);
  typedef void*         (__cdecl *PFN_FPDFBitmap_GetBuffer)(FPDF_BITMAP bitmap);
  typedef int           (__cdecl *PFN_FPDFBitmap_GetStride)(FPDF_BITMAP bitmap);
  typedef void          (__cdecl *PFN_FPDFBitmap_Destroy)(FPDF_BITMAP bitmap);

  HMODULE                    m_hModule;
  bool                       m_bAttemptedLoad;
  bool                       m_bInitialized;
  PFN_FPDF_InitLibrary       m_pInitLibrary;
  PFN_FPDF_DestroyLibrary    m_pDestroyLibrary;
  PFN_FPDF_LoadDocument      m_pLoadDocument;
  PFN_FPDF_CloseDocument     m_pCloseDocument;
  PFN_FPDF_GetPageCount      m_pGetPageCount;
  PFN_FPDF_LoadPage          m_pLoadPage;
  PFN_FPDF_ClosePage         m_pClosePage;
  PFN_FPDF_GetPageWidth      m_pGetPageWidth;
  PFN_FPDF_GetPageHeight     m_pGetPageHeight;
  PFN_FPDFBitmap_CreateEx    m_pBitmapCreateEx;
  PFN_FPDFBitmap_FillRect    m_pBitmapFillRect;
  PFN_FPDF_RenderPageBitmap  m_pRenderPageBitmap;
  PFN_FPDFBitmap_GetBuffer   m_pBitmapGetBuffer;
  PFN_FPDFBitmap_GetStride   m_pBitmapGetStride;
  PFN_FPDFBitmap_Destroy     m_pBitmapDestroy;
#endif
};

#endif // __CPDF_RENDERER_H__
