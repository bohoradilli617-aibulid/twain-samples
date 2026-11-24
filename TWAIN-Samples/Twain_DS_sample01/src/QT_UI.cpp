/***************************************************************************
* Copyright � 2007 TWAIN Working Group:  
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
* @file CTWAINDS_FreeImage.cpp
* The main Data Source class.
* This class is derived from the Base class describing a TWAIN DS.
* @author TWAIN Working Group
* @date April 2007
*/
#include "CommonDS.h"
#include "QT_UI.h"
#include "Qt_MainForm.h"
#include <QtCore/QMetaObject>

#if defined(TWNDS_OS_WIN)
#include <objbase.h>
extern HINSTANCE g_hinstance;
#endif

namespace
{
  static int   s_qtArgc   = 1;
  static char  s_qtArg0[] = "VirtualScanner";
  static char* s_qtArgv[] = { s_qtArg0, NULL };

#if defined(TWNDS_OS_WIN)
  void addQtPluginLibraryPath()
  {
    HMODULE hmodule = (HMODULE)g_hinstance;
    if (!hmodule)
    {
      hmodule = ::GetModuleHandleA("TWAINDS_Sample32.ds");
    }
    if (!hmodule)
    {
      hmodule = ::GetModuleHandleA("TWAINDS_Sample64.ds");
    }
    if (hmodule)
    {
      char szDs[PATH_MAX] = {0};
      DWORD len = ::GetModuleFileNameA(hmodule, szDs, sizeof(szDs) - 1);
      if (len > 0)
      {
        char *szToken = strrchr(szDs, '\\');
        if (szToken)
        {
          szToken[0] = 0; // folder where the .ds lives
          QCoreApplication::addLibraryPath(QString::fromLocal8Bit(szDs));
        }
      }
    }
  }

  INIT_ONCE        s_modalHostInitOnce = INIT_ONCE_STATIC_INIT;
  CRITICAL_SECTION s_modalHostCs;
  CRITICAL_SECTION s_modalDialogCs;
  HANDLE           s_hModalHostThread  = NULL;
  bool             s_bModalHostReady   = false;

  BOOL CALLBACK initModalHostSync(PINIT_ONCE, PVOID, PVOID*)
  {
    ::InitializeCriticalSection(&s_modalHostCs);
    ::InitializeCriticalSection(&s_modalDialogCs);
    return TRUE;
  }

  DWORD WINAPI qtModalHostThreadProc(LPVOID lpParam)
  {
    HANDLE hReadyEvent = (HANDLE)lpParam;
    HRESULT hrCom = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    addQtPluginLibraryPath();

    QApplication app(s_qtArgc, s_qtArgv);
    app.setQuitOnLastWindowClosed(false);

    s_bModalHostReady = true;
    if (hReadyEvent)
    {
      ::SetEvent(hReadyEvent);
    }

    app.exec();

    s_bModalHostReady = false;
    if (SUCCEEDED(hrCom))
    {
      ::CoUninitialize();
    }
    return 0;
  }

  bool ensureModalQtHostStarted()
  {
    ::InitOnceExecuteOnce(&s_modalHostInitOnce, initModalHostSync, NULL, NULL);
    ::EnterCriticalSection(&s_modalHostCs);

    if (s_hModalHostThread != NULL && s_bModalHostReady && qApp != NULL)
    {
      ::LeaveCriticalSection(&s_modalHostCs);
      return true;
    }

    if (qApp != NULL)
    {
      // Host process already owns a QApplication instance.
      ::LeaveCriticalSection(&s_modalHostCs);
      return true;
    }

    // Pin this .ds module in the host process so FreeLibrary cannot unmap
    // our persistent STA Qt UI thread between scans.
    HMODULE hPinned = NULL;
    ::GetModuleHandleExA(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
      (LPCSTR)&qtModalHostThreadProc,
      &hPinned);

    HANDLE hReadyEvent = ::CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!hReadyEvent)
    {
      ::LeaveCriticalSection(&s_modalHostCs);
      return false;
    }

    DWORD threadId = 0;
    s_hModalHostThread = ::CreateThread(NULL, 0, qtModalHostThreadProc, hReadyEvent, 0, &threadId);
    if (!s_hModalHostThread)
    {
      ::CloseHandle(hReadyEvent);
      ::LeaveCriticalSection(&s_modalHostCs);
      return false;
    }

    DWORD waitRes = ::WaitForSingleObject(hReadyEvent, 15000);
    ::CloseHandle(hReadyEvent);

    bool ok = (waitRes == WAIT_OBJECT_0 && s_bModalHostReady && qApp != NULL);
    ::LeaveCriticalSection(&s_modalHostCs);
    return ok;
  }
#endif
}

CTWAIN_UI* CreateUI(CTWAINDS_FreeImage *pDS)
{
  return new QT_UI(pDS);
}

void DestroyUI(CTWAIN_UI* pUI)
{
  if(pUI)
  {
    delete ((QT_UI*)pUI);
  }
}

void UIThread::run()
{
  m_pApp = new QApplication(s_qtArgc, s_qtArgv);
  if(m_Data.ShowUI)
  {
    m_pDlg = new MainForm(m_pUI);
    m_pDlg->show();
    m_pDlg->raise();
    m_pDlg->activateWindow();
    m_pApp->exec();
  }
  delete m_pApp;
  m_pApp = 0;
}

QT_UI::QT_UI(CTWAINDS_FreeImage *pDS):CTWAIN_UI(pDS)
{
  m_pDlg=NULL;
  m_pChildWnd=NULL;
  m_pUIThread=NULL;
  m_pQtApp = NULL;
}

QT_UI::~QT_UI()
{
  if(m_pQtApp)
  {
    delete m_pQtApp;
	m_pQtApp = 0;
    //qApp = NULL; QT will do it in QCoreApplication destructor
  }
}

bool QT_UI::RunModalForceUI(ModalForceUIRequest& request)
{
  request.accepted = false;
  request.selectedPages.clear();

#if defined(TWNDS_OS_WIN)
  if (!ensureModalQtHostStarted() || !qApp)
  {
    return false;
  }

  ::EnterCriticalSection(&s_modalDialogCs);

  if (QThread::currentThread() == qApp->thread())
  {
    MainForm dlg(&request, NULL);
    dlg.exec();
  }
  else
  {
    HANDLE hDoneEvent = ::CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!hDoneEvent)
    {
      ::LeaveCriticalSection(&s_modalDialogCs);
      return false;
    }

    ModalForceUIRequest *pReq = &request;
    bool queued = QMetaObject::invokeMethod(qApp, [pReq, hDoneEvent]() {
      MainForm dlg(pReq, NULL);
      dlg.exec();
      ::SetEvent(hDoneEvent);
    }, Qt::QueuedConnection);

    if (queued)
    {
      ::WaitForSingleObject(hDoneEvent, INFINITE);
    }
    ::CloseHandle(hDoneEvent);
  }

  ::LeaveCriticalSection(&s_modalDialogCs);
  return request.accepted;
#else
  return false;
#endif
}

TW_INT16 QT_UI::DisplayTWAINGUI(TW_USERINTERFACE Data, bool bSetup, bool bIndicators)
{
  TW_INT16 nRes = CTWAIN_UI::DisplayTWAINGUI(Data, bSetup, bIndicators);
  if(nRes)
  {
    return nRes;
  }
  if(bSetup)
  {
    Data.ShowUI=1;
  }

  if(Data.ShowUI==0 && !bIndicators)
  {
    return TWRC_SUCCESS;
  }
  QApplication * pApp = qApp;
  if(qApp)
  {
    int nV1App=0, nV2App=0, nV3App=0;
    SSCANF(qVersion(),"%d.%d.%d",&nV1App, &nV2App, &nV3App);
    int nV1DS=(BYTE)(QT_VERSION>>16);
    int nV2DS=(BYTE)(QT_VERSION>>8);
    int nV3DS=(BYTE)QT_VERSION;
    if(nV1App<nV1DS || (nV1App==nV1DS && nV2App<nV2DS))
    {
      return TWRC_FAILURE;
    }
  }
#if defined (TWNDS_OS_WIN) || defined (TWNDS_OS_APPLE)
  if (!pApp)
  {
#if defined(TWNDS_OS_WIN)
    addQtPluginLibraryPath();
#endif
    pApp = m_pQtApp = new QApplication(s_qtArgc, s_qtArgv);
//	  m_pQtApp->setAttribute(Qt::AA_DontUseNativeMenuBar,true);
	  m_pQtApp->setAttribute(Qt::AA_MacPluginApplication,true);
  }
#endif
  m_pChildWnd = NULL;
  if(!pApp)
  {
#if defined (TWNDS_OS_WIN)
    return TWRC_FAILURE;
#elif defined(TWNDS_OS_LINUX)// || defined (TWNDS_OS_APPLE)
    m_pUIThread = new UIThread(this, Data);
    m_pUIThread->start();   
#endif
  }
  else
  {
#if defined(TWNDS_OS_WIN) //|| defined (TWNDS_OS_APPLE)
    if(Data.hParent && ::IsWindow((HWND)Data.hParent) && ::IsWindowVisible((HWND)Data.hParent) && !::IsIconic((HWND)Data.hParent))
    {
      m_pChildWnd = new QWidget();
      long ll =  GetWindowLong((HWND)(m_pChildWnd->winId()),GWL_STYLE);
      SetWindowLong((HWND)(m_pChildWnd->winId()), GWL_STYLE, WS_CHILD|ll);
      SetParent((HWND)(m_pChildWnd->winId()), (HWND)Data.hParent);
    }
#endif
    
    if(Data.ShowUI)
    {
      m_pDlg = new MainForm(this,m_pChildWnd);
      if(m_pDlg)
      {
        m_pDlg->show();
        m_pDlg->raise();
        m_pDlg->activateWindow();
#if defined(TWNDS_OS_WIN)
        HWND hDlg = (HWND)m_pDlg->winId();
        if (hDlg)
        {
          ::SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
          ::SetForegroundWindow(hDlg);
        }
#endif
      }
      else
      {
        return TWRC_FAILURE;
      }
    }
  }
  return TWRC_SUCCESS;
}

void QT_UI::DestroyTWAINGUI()
{
  CTWAIN_UI::DestroyTWAINGUI();
  
  if(m_pDlg)
  {
    delete m_pDlg;
    m_pDlg = NULL;
  }
  if(m_pChildWnd)
  {
    delete m_pChildWnd;
    m_pChildWnd = NULL;
  }
  if(m_pUIThread)
  {
    QEvent *pQEvent = new QEvent(QEvent::Quit);
   // pQEvent->setAccepted(false);
    m_pUIThread->m_pApp->postEvent(m_pUIThread->m_pApp,pQEvent);

    while(m_pUIThread->isRunning())
       ;
    delete m_pUIThread;
	m_pUIThread = 0;
  }
	if(m_pQtApp)
	{
		m_pQtApp->flush();
		m_pQtApp->exit();
		delete m_pQtApp;
		m_pQtApp = NULL;
	}
}

void QT_UI::UpdateProgress(bool bShow, unsigned char ucProgress, unsigned int unPageNo, string strProgressTitle)
{
  ;
}

unsigned int QT_UI::MessageBox(string strMessage,string strTitle, unsigned int unIconID)
{
  QMessageBox msgBox(QMessageBox::NoIcon, strTitle.c_str(),strMessage.c_str());
  return msgBox.exec();
}

bool QT_UI::processEvent(pTW_EVENT _pEvent)
{
#if defined (TWNDS_OS_WIN)
  if(qApp)
  {
    qApp->sendPostedEvents();
  }
#endif
  return false;
}
