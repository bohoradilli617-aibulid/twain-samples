#include "CPdfRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>

#ifdef _WIN32
extern HINSTANCE g_hinstance;
#endif

namespace
{
  const int kFPDFBitmap_BGR = 2;
  const int kFPDF_ANNOT     = 0x01;
  const int kFPDF_PRINTING  = 0x800;

  std::wstring toLowerWide(const std::wstring& value)
  {
    std::wstring result = value;
    for (size_t i = 0; i < result.size(); ++i)
    {
      result[i] = (wchar_t)towlower(result[i]);
    }
    return result;
  }

  bool endsWithWide(const std::wstring& value, const std::wstring& suffix)
  {
    if (value.size() < suffix.size())
    {
      return false;
    }
    return toLowerWide(value.substr(value.size() - suffix.size())) == suffix;
  }
}

CPdfRenderer& CPdfRenderer::instance()
{
  static CPdfRenderer s_instance;
  return s_instance;
}

bool CPdfRenderer::isPdfPath(const std::wstring& filePath)
{
  return endsWithWide(filePath, L".pdf");
}

bool CPdfRenderer::isTiffPath(const std::wstring& filePath)
{
  return endsWithWide(filePath, L".tif") || endsWithWide(filePath, L".tiff");
}

CPdfRenderer::CPdfRenderer()
#ifdef _WIN32
  : m_hModule(NULL)
  , m_bAttemptedLoad(false)
  , m_bInitialized(false)
  , m_pInitLibrary(NULL)
  , m_pDestroyLibrary(NULL)
  , m_pLoadDocument(NULL)
  , m_pCloseDocument(NULL)
  , m_pGetPageCount(NULL)
  , m_pLoadPage(NULL)
  , m_pClosePage(NULL)
  , m_pGetPageWidth(NULL)
  , m_pGetPageHeight(NULL)
  , m_pBitmapCreateEx(NULL)
  , m_pBitmapFillRect(NULL)
  , m_pRenderPageBitmap(NULL)
  , m_pBitmapGetBuffer(NULL)
  , m_pBitmapGetStride(NULL)
  , m_pBitmapDestroy(NULL)
#endif
{
}

CPdfRenderer::~CPdfRenderer()
{
#ifdef _WIN32
  if (m_bInitialized && m_pDestroyLibrary)
  {
    m_pDestroyLibrary();
    m_bInitialized = false;
  }
  if (m_hModule)
  {
    FreeLibrary(m_hModule);
    m_hModule = NULL;
  }
#endif
}

#ifdef _WIN32
std::string CPdfRenderer::wideToUtf8(const std::wstring& wide)
{
  if (wide.empty())
  {
    return std::string();
  }
  int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
  if (required <= 0)
  {
    return std::string();
  }
  std::string utf8(required, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &utf8[0], required, NULL, NULL);
  return utf8;
}

std::wstring CPdfRenderer::getDsDirectory()
{
  wchar_t modulePath[MAX_PATH * 4] = {0};
  HMODULE hMod = (HMODULE)g_hinstance;
  if (!hMod)
  {
    hMod = GetModuleHandleW(L"TWAINDS_Sample32.ds");
  }
  if (!hMod)
  {
    hMod = GetModuleHandleW(L"TWAINDS_Sample64.ds");
  }
  DWORD len = GetModuleFileNameW(hMod, modulePath, (DWORD)(sizeof(modulePath) / sizeof(modulePath[0])));
  if (len == 0)
  {
    return std::wstring();
  }
  std::wstring full(modulePath, len);
  size_t pos = full.find_last_of(L"\\/");
  if (pos != std::wstring::npos)
  {
    return full.substr(0, pos);
  }
  return std::wstring();
}
#endif

bool CPdfRenderer::ensureLoaded()
{
#ifdef _WIN32
  if (m_bInitialized)
  {
    return true;
  }
  if (m_bAttemptedLoad)
  {
    return false;
  }
  m_bAttemptedLoad = true;

  std::wstring dsDir = getDsDirectory();
  if (!dsDir.empty())
  {
    std::wstring dllPath = dsDir + L"\\pdfium.dll";
    m_hModule = LoadLibraryW(dllPath.c_str());
  }
  if (!m_hModule)
  {
    m_hModule = LoadLibraryW(L"pdfium.dll");
  }
  if (!m_hModule)
  {
    return false;
  }

  m_pInitLibrary      = (PFN_FPDF_InitLibrary)      GetProcAddress(m_hModule, "FPDF_InitLibrary");
  m_pDestroyLibrary   = (PFN_FPDF_DestroyLibrary)   GetProcAddress(m_hModule, "FPDF_DestroyLibrary");
  m_pLoadDocument     = (PFN_FPDF_LoadDocument)     GetProcAddress(m_hModule, "FPDF_LoadDocument");
  m_pCloseDocument    = (PFN_FPDF_CloseDocument)    GetProcAddress(m_hModule, "FPDF_CloseDocument");
  m_pGetPageCount     = (PFN_FPDF_GetPageCount)     GetProcAddress(m_hModule, "FPDF_GetPageCount");
  m_pLoadPage         = (PFN_FPDF_LoadPage)         GetProcAddress(m_hModule, "FPDF_LoadPage");
  m_pClosePage        = (PFN_FPDF_ClosePage)        GetProcAddress(m_hModule, "FPDF_ClosePage");
  m_pGetPageWidth     = (PFN_FPDF_GetPageWidth)     GetProcAddress(m_hModule, "FPDF_GetPageWidth");
  m_pGetPageHeight    = (PFN_FPDF_GetPageHeight)    GetProcAddress(m_hModule, "FPDF_GetPageHeight");
  m_pBitmapCreateEx   = (PFN_FPDFBitmap_CreateEx)   GetProcAddress(m_hModule, "FPDFBitmap_CreateEx");
  m_pBitmapFillRect   = (PFN_FPDFBitmap_FillRect)   GetProcAddress(m_hModule, "FPDFBitmap_FillRect");
  m_pRenderPageBitmap = (PFN_FPDF_RenderPageBitmap) GetProcAddress(m_hModule, "FPDF_RenderPageBitmap");
  m_pBitmapGetBuffer  = (PFN_FPDFBitmap_GetBuffer)  GetProcAddress(m_hModule, "FPDFBitmap_GetBuffer");
  m_pBitmapGetStride  = (PFN_FPDFBitmap_GetStride)  GetProcAddress(m_hModule, "FPDFBitmap_GetStride");
  m_pBitmapDestroy    = (PFN_FPDFBitmap_Destroy)    GetProcAddress(m_hModule, "FPDFBitmap_Destroy");

  if (!m_pInitLibrary || !m_pDestroyLibrary || !m_pLoadDocument || !m_pCloseDocument ||
      !m_pGetPageCount || !m_pLoadPage || !m_pClosePage || !m_pGetPageWidth ||
      !m_pGetPageHeight || !m_pBitmapCreateEx || !m_pBitmapFillRect ||
      !m_pRenderPageBitmap || !m_pBitmapGetBuffer || !m_pBitmapGetStride || !m_pBitmapDestroy)
  {
    FreeLibrary(m_hModule);
    m_hModule = NULL;
    return false;
  }

  m_pInitLibrary();
  m_bInitialized = true;
  return true;
#else
  return false;
#endif
}

int CPdfRenderer::getPageCount(const std::wstring& filePath)
{
#ifdef _WIN32
  if (!ensureLoaded())
  {
    return 0;
  }
  std::string utf8 = wideToUtf8(filePath);
  if (utf8.empty())
  {
    return 0;
  }
  FPDF_DOCUMENT doc = m_pLoadDocument(utf8.c_str(), NULL);
  if (!doc)
  {
    return 0;
  }
  int count = m_pGetPageCount(doc);
  m_pCloseDocument(doc);
  return (count > 0) ? count : 0;
#else
  (void)filePath;
  return 0;
#endif
}

FIBITMAP* CPdfRenderer::renderPageToDIB(const std::wstring& filePath, int pageIndex, float dpiX, float dpiY)
{
#ifdef _WIN32
  if (!ensureLoaded() || pageIndex < 0)
  {
    return NULL;
  }

  if (dpiX < 50.0f) dpiX = 200.0f;
  if (dpiY < 50.0f) dpiY = 200.0f;
  if (dpiX > 600.0f) dpiX = 600.0f;
  if (dpiY > 600.0f) dpiY = 600.0f;

  std::string utf8 = wideToUtf8(filePath);
  if (utf8.empty())
  {
    return NULL;
  }

  FPDF_DOCUMENT doc = m_pLoadDocument(utf8.c_str(), NULL);
  if (!doc)
  {
    return NULL;
  }

  int pageCount = m_pGetPageCount(doc);
  if (pageIndex >= pageCount)
  {
    m_pCloseDocument(doc);
    return NULL;
  }

  FPDF_PAGE page = m_pLoadPage(doc, pageIndex);
  if (!page)
  {
    m_pCloseDocument(doc);
    return NULL;
  }

  double widthPts  = m_pGetPageWidth(page);
  double heightPts = m_pGetPageHeight(page);
  int widthPx  = (int)std::floor(widthPts  * (dpiX / 72.0) + 0.5);
  int heightPx = (int)std::floor(heightPts * (dpiY / 72.0) + 0.5);

  if (widthPx < 1)  widthPx  = 1;
  if (heightPx < 1) heightPx = 1;

  // Keep memory footprint safe in 32-bit x86 address space
  const int kMaxDimension = 12000;
  if (widthPx > kMaxDimension || heightPx > kMaxDimension)
  {
    double scale = (double)kMaxDimension / (double)((widthPx > heightPx) ? widthPx : heightPx);
    widthPx  = (int)std::floor(widthPx  * scale);
    heightPx = (int)std::floor(heightPx * scale);
    dpiX = (float)(dpiX * scale);
    dpiY = (float)(dpiY * scale);
  }

  FPDF_BITMAP bitmap = m_pBitmapCreateEx(widthPx, heightPx, kFPDFBitmap_BGR, NULL, 0);
  if (!bitmap)
  {
    m_pClosePage(page);
    m_pCloseDocument(doc);
    return NULL;
  }

  // Fill with opaque white background before rendering page content
  m_pBitmapFillRect(bitmap, 0, 0, widthPx, heightPx, 0xFFFFFFFF);
  m_pRenderPageBitmap(bitmap, page, 0, 0, widthPx, heightPx, 0, kFPDF_ANNOT | kFPDF_PRINTING);

  const unsigned char* srcBuffer = (const unsigned char*)m_pBitmapGetBuffer(bitmap);
  int srcStride = m_pBitmapGetStride(bitmap);

  FIBITMAP* dib = NULL;
  if (srcBuffer && srcStride >= widthPx * 3)
  {
    dib = FreeImage_Allocate(widthPx, heightPx, 24);
    if (dib)
    {
      const size_t rowBytes = (size_t)widthPx * 3;
      for (int y = 0; y < heightPx; ++y)
      {
        // PDFium is top-down BGR; FreeImage 24-bit DIB is bottom-up BGR
        BYTE* dstLine = FreeImage_GetScanLine(dib, heightPx - 1 - y);
        const unsigned char* srcLine = srcBuffer + (size_t)y * (size_t)srcStride;
        std::memcpy(dstLine, srcLine, rowBytes);
      }

      FreeImage_SetDotsPerMeterX(dib, (unsigned)(dpiX * 39.3700787f + 0.5f));
      FreeImage_SetDotsPerMeterY(dib, (unsigned)(dpiY * 39.3700787f + 0.5f));
    }
  }

  m_pBitmapDestroy(bitmap);
  m_pClosePage(page);
  m_pCloseDocument(doc);
  return dib;
#else
  (void)filePath;
  (void)pageIndex;
  (void)dpiX;
  (void)dpiY;
  return NULL;
#endif
}
