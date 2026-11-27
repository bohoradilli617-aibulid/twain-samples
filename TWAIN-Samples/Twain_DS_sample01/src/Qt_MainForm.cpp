#include "Qt_MainForm.h"
#include "ui_MainForm.h"
#include "CPdfRenderer.h"

#include <QCloseEvent>
#include <QCursor>
#include <QHideEvent>
#include <QLineEdit>
#include <QMetaObject>
#include <QShowEvent>
#include <QStringList>
#include <QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QMessageBox>
#include <algorithm>
#include <functional>
#include <set>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace
{
  const int kModeAllPages    = 0;
  const int kModeFirstPage   = 1;
  const int kModeCustomRange = 2;

  std::wstring s_lastPickerDir;

  QString baseFileName(const std::wstring& path)
  {
    size_t pos = path.find_last_of(L"\\/");
    std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
    return QString::fromStdWString(name);
  }

#ifdef _WIN32
  void forceWindowToForeground(HWND hWnd)
  {
    if (!hWnd || !::IsWindow(hWnd))
    {
      return;
    }

    ::AllowSetForegroundWindow(ASFW_ANY);
    ::ShowWindow(hWnd, SW_RESTORE);
    ::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    HWND hFg = ::GetForegroundWindow();
    DWORD fgTid = hFg ? ::GetWindowThreadProcessId(hFg, NULL) : 0;
    DWORD curTid = ::GetCurrentThreadId();
    bool attached = false;
    if (fgTid && fgTid != curTid)
    {
      attached = (::AttachThreadInput(curTid, fgTid, TRUE) != FALSE);
    }

    ::BringWindowToTop(hWnd);
    ::SetForegroundWindow(hWnd);
    ::SetActiveWindow(hWnd);

    if (attached)
    {
      ::AttachThreadInput(curTid, fgTid, FALSE);
    }
  }
#endif
}

MainForm::MainForm(QT_UI *pUI, QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::MainForm)
  , m_pUI(pUI)
  , m_pModalRequest(NULL)
  , m_bScanStarted(false)
  , m_bFirstShow(true)
  , m_bEsclInitializing(false)
  , m_bEsclScanning(false)
{
  ui->setupUi(this);
  setWindowFlags((windowFlags() | Qt::Dialog | Qt::WindowStaysOnTopHint) & ~Qt::WindowContextHelpButtonHint);

  if (m_pUI && m_pUI->m_bSetup)
  {
    ui->pPushButton_Scan->setText(QApplication::translate("MainForm", "Save", 0));
  }

  UpdateControls();
  InitEsclControls();
  UpdateQueueSummary();
}

MainForm::MainForm(ModalForceUIRequest *pModalRequest, QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::MainForm)
  , m_pUI(NULL)
  , m_pModalRequest(pModalRequest)
  , m_bScanStarted(false)
  , m_bFirstShow(true)
  , m_bEsclInitializing(false)
  , m_bEsclScanning(false)
{
  ui->setupUi(this);
  setWindowFlags((windowFlags() | Qt::Dialog | Qt::WindowStaysOnTopHint) & ~Qt::WindowContextHelpButtonHint);

  PopulateModalControls();
  InitEsclControls();
  UpdateQueueSummary();
}

MainForm::~MainForm()
{
  StopEsclWorker();
  delete ui;
}

void MainForm::BringDialogToFront()
{
  raise();
  activateWindow();
#ifdef _WIN32
  forceWindowToForeground((HWND)this->winId());
#endif
}

void MainForm::showEvent(QShowEvent *evShow)
{
  QDialog::showEvent(evShow);

  if (m_bFirstShow)
  {
    m_bFirstShow = false;
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
    {
      screen = QGuiApplication::primaryScreen();
    }
    if (screen)
    {
      QRect avail = screen->availableGeometry();
      move(avail.center() - rect().center());
    }

    BringDialogToFront();
    QTimer::singleShot(50, this, [this]() {
      if (isVisible())
      {
        BringDialogToFront();
      }
    });
  }
}

bool MainForm::OpenNativeMultiFilePicker()
{
#ifdef _WIN32
  std::vector<wchar_t> fileBuffer(65536, L'\0');

  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize     = sizeof(ofn);
  ofn.hwndOwner       = (HWND)this->winId();
  ofn.lpstrFilter     =
    L"Supported Documents & Images (*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff)\0*.pdf;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0"
    L"PDF Documents (*.pdf)\0*.pdf\0"
    L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0"
    L"All Files (*.*)\0*.*\0\0";
  ofn.lpstrFile       = &fileBuffer[0];
  ofn.nMaxFile        = (DWORD)fileBuffer.size();
  ofn.lpstrTitle      = L"Select PDF or Image Files to Scan";
  ofn.lpstrInitialDir = s_lastPickerDir.empty() ? NULL : s_lastPickerDir.c_str();
  ofn.Flags           = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  BOOL picked = GetOpenFileNameW(&ofn);
  BringDialogToFront();
  if (!picked)
  {
    return false;
  }

  std::vector<std::wstring> pickedPaths;
  const wchar_t* p = &fileBuffer[0];
  std::wstring firstToken(p);
  p += firstToken.size() + 1;

  if (*p == L'\0')
  {
    // Single file selected
    pickedPaths.push_back(firstToken);
    size_t slash = firstToken.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
      s_lastPickerDir = firstToken.substr(0, slash);
    }
  }
  else
  {
    // Multiple files selected: firstToken is directory, followed by null-separated filenames
    std::wstring dir = firstToken;
    s_lastPickerDir = dir;
    if (!dir.empty() && dir[dir.size() - 1] != L'\\' && dir[dir.size() - 1] != L'/')
    {
      dir += L'\\';
    }
    while (*p != L'\0')
    {
      std::wstring fileName(p);
      pickedPaths.push_back(dir + fileName);
      p += fileName.size() + 1;
    }
  }

  QStringList failedFiles;
  bool pdfEngineMissing = false;
  bool addedAny = false;

  for (size_t i = 0; i < pickedPaths.size(); ++i)
  {
    bool isPdf = false;
    bool isMultiTiff = false;
    int pageCount = CScanner_FreeImage::getFilePageCount(pickedPaths[i], isPdf, isMultiTiff);
    if (pageCount <= 0)
    {
      if (isPdf && !CPdfRenderer::instance().ensureLoaded())
      {
        pdfEngineMissing = true;
      }
      failedFiles << baseFileName(pickedPaths[i]);
      continue;
    }

    SelectedFileEntry entry;
    entry.filePath    = pickedPaths[i];
    entry.pageCount   = pageCount;
    entry.isPdf       = isPdf;
    entry.isMultiTiff = isMultiTiff;
    m_files.push_back(entry);
    addedAny = true;
  }

  RefreshFileList();

  if (!failedFiles.isEmpty())
  {
    QString msg = QString("Could not open the following file(s):\n%1").arg(failedFiles.join("\n"));
    if (pdfEngineMissing)
    {
      msg += "\n\nNote: 32-bit pdfium.dll was not found in the TWAIN data source folder.";
    }
    QMessageBox::warning(this, "Virtual Scanner", msg);
  }

  return addedAny;
#else
  return false;
#endif
}

void MainForm::RefreshFileList()
{
  int prevRow = ui->pListWidget_Files->currentRow();
  ui->pListWidget_Files->clear();

  for (size_t i = 0; i < m_files.size(); ++i)
  {
    const SelectedFileEntry& entry = m_files[i];
    QString label = QString("%1  (%2 %3)")
      .arg(baseFileName(entry.filePath))
      .arg(entry.pageCount)
      .arg(entry.pageCount == 1 ? "page" : "pages");

    QListWidgetItem* item = new QListWidgetItem(label);
    item->setToolTip(QString::fromStdWString(entry.filePath));
    ui->pListWidget_Files->addItem(item);
  }

  if (!m_files.empty())
  {
    int targetRow = (prevRow >= 0 && prevRow < (int)m_files.size()) ? prevRow : (int)m_files.size() - 1;
    ui->pListWidget_Files->setCurrentRow(targetRow);
  }

  UpdateQueueSummary();
}

bool MainForm::ParsePageRange(const QString& rangeText, int totalPages, std::vector<int>& outZeroBasedIndices)
{
  outZeroBasedIndices.clear();
  if (totalPages <= 0)
  {
    return false;
  }

  QString trimmed = rangeText.trimmed();
  if (trimmed.isEmpty())
  {
    return false;
  }

  QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
  if (parts.isEmpty())
  {
    return false;
  }

  for (int i = 0; i < parts.size(); ++i)
  {
    QString token = parts[i].trimmed();
    int dashPos = token.indexOf('-');
    if (dashPos >= 0)
    {
      bool okStart = false;
      bool okEnd = false;
      int startPage = token.left(dashPos).trimmed().toInt(&okStart);
      int endPage   = token.mid(dashPos + 1).trimmed().toInt(&okEnd);
      if (!okStart || !okEnd || startPage < 1 || endPage < 1 || startPage > endPage)
      {
        return false;
      }
      for (int p = startPage; p <= endPage && p <= totalPages; ++p)
      {
        outZeroBasedIndices.push_back(p - 1);
      }
    }
    else
    {
      bool ok = false;
      int pageNum = token.toInt(&ok);
      if (!ok || pageNum < 1)
      {
        return false;
      }
      if (pageNum <= totalPages)
      {
        outZeroBasedIndices.push_back(pageNum - 1);
      }
    }
  }

  return !outZeroBasedIndices.empty();
}

bool MainForm::BuildQueuedPages(std::vector<ScanPageItem>& outPages, QString& outError) const
{
  outPages.clear();
  outError.clear();

  if (m_files.empty())
  {
    outError = "Please select at least one PDF or image file to scan.";
    return false;
  }

  int mode = ui->pComboBox_MultiPageMode->currentIndex();

  if (mode == kModeFirstPage)
  {
    for (size_t i = 0; i < m_files.size(); ++i)
    {
      const SelectedFileEntry& entry = m_files[i];
      outPages.push_back(ScanPageItem(entry.filePath, 0, entry.isPdf, entry.isMultiTiff));
    }
    return !outPages.empty();
  }

  // Expand all pages in file order
  std::vector<ScanPageItem> allPages;
  for (size_t i = 0; i < m_files.size(); ++i)
  {
    const SelectedFileEntry& entry = m_files[i];
    for (int p = 0; p < entry.pageCount; ++p)
    {
      allPages.push_back(ScanPageItem(entry.filePath, p, entry.isPdf, entry.isMultiTiff));
    }
  }

  if (mode == kModeAllPages)
  {
    outPages = allPages;
    return !outPages.empty();
  }

  // Custom page range (1-based across queued pages)
  std::vector<int> indices;
  if (!ParsePageRange(ui->pLineEdit_PageRange->text(), (int)allPages.size(), indices))
  {
    outError = QString("Invalid page range. Enter pages between 1 and %1 (e.g. 1-3 or 1,3).")
      .arg((int)allPages.size());
    return false;
  }

  for (size_t i = 0; i < indices.size(); ++i)
  {
    outPages.push_back(allPages[(size_t)indices[i]]);
  }
  return !outPages.empty();
}

void MainForm::UpdateQueueSummary()
{
  bool hasFiles = !m_files.empty();
  bool allowEdit = !m_bEsclScanning;
  ui->pPushButton_MoveUp->setEnabled(hasFiles && allowEdit);
  ui->pPushButton_MoveDown->setEnabled(hasFiles && allowEdit);
  ui->pPushButton_RemoveFile->setEnabled(hasFiles && allowEdit);
  ui->pPushButton_ClearFiles->setEnabled(hasFiles && allowEdit);

  if (m_bEsclScanning)
  {
    return;
  }

  if (!hasFiles)
  {
    ui->pLabel_QueueSummary->setText("No files selected. Click \"Add Files...\" or \"Scan from Network\".");
    return;
  }

  int totalSourcePages = 0;
  for (size_t i = 0; i < m_files.size(); ++i)
  {
    totalSourcePages += m_files[i].pageCount;
  }

  std::vector<ScanPageItem> queued;
  QString err;
  if (BuildQueuedPages(queued, err))
  {
    ui->pLabel_QueueSummary->setText(
      QString("Selected: %1 file(s), %2 total page(s)  |  Will scan: %3 page(s)")
        .arg((int)m_files.size())
        .arg(totalSourcePages)
        .arg((int)queued.size()));
  }
  else
  {
    ui->pLabel_QueueSummary->setText(
      QString("Selected: %1 file(s), %2 total page(s)  |  %3")
        .arg((int)m_files.size())
        .arg(totalSourcePages)
        .arg(err));
  }
}

void MainForm::InitEsclControls()
{
  m_bEsclInitializing = true;
  m_esclSettings = CEsclClient::loadSettings();

  if (ui->pComboBox_EsclUrl->lineEdit())
  {
    ui->pComboBox_EsclUrl->lineEdit()->setPlaceholderText("e.g. 192.168.1.100 or http://192.168.1.100/eSCL");
  }

  RefreshEsclCombo();

  int srcIdx = m_esclSettings.lastSource;
  if (srcIdx < 0 || srcIdx >= ui->pComboBox_EsclSource->count())
  {
    srcIdx = 0;
  }
  ui->pComboBox_EsclSource->setCurrentIndex(srcIdx);

  int paperIdx = m_esclSettings.lastPaper;
  if (paperIdx < 0 || paperIdx >= ui->pComboBox_EsclPaper->count())
  {
    paperIdx = 0;
  }
  ui->pComboBox_EsclPaper->setCurrentIndex(paperIdx);
  ui->pCheckBox_EsclAutoSend->setChecked(m_esclSettings.autoSend);

  if (m_pUI && m_pUI->m_bSetup)
  {
    ui->pGroupBox_Escl->setEnabled(false);
  }

  m_bEsclInitializing = false;
}

void MainForm::RefreshEsclCombo()
{
  bool prevInit = m_bEsclInitializing;
  m_bEsclInitializing = true;

  ui->pComboBox_EsclUrl->clear();
  for (size_t i = 0; i < m_esclSettings.scanners.size(); ++i)
  {
    const EsclScannerEntry& entry = m_esclSettings.scanners[i];
    QString url = QString::fromStdString(entry.url);
    QString label = entry.name.empty()
      ? url
      : QString("%1 - %2").arg(QString::fromStdString(entry.name), url);
    ui->pComboBox_EsclUrl->addItem(label, url);
  }

  if (ui->pComboBox_EsclUrl->count() > 0)
  {
    ui->pComboBox_EsclUrl->setCurrentIndex(0);
  }
  else
  {
    ui->pComboBox_EsclUrl->setEditText(QString());
  }

  m_bEsclInitializing = prevInit;
}

std::string MainForm::GetSelectedEsclTarget() const
{
  QString text = ui->pComboBox_EsclUrl->currentText().trimmed();
  if (text.isEmpty())
  {
    return std::string();
  }

  for (int i = 0; i < ui->pComboBox_EsclUrl->count(); ++i)
  {
    if (ui->pComboBox_EsclUrl->itemText(i) == text)
    {
      QString storedUrl = ui->pComboBox_EsclUrl->itemData(i).toString().trimmed();
      if (!storedUrl.isEmpty())
      {
        return storedUrl.toStdString();
      }
    }
  }

  int httpIdx = text.indexOf(" - http://", 0, Qt::CaseInsensitive);
  if (httpIdx >= 0)
  {
    return text.mid(httpIdx + 3).trimmed().toStdString();
  }
  int httpsIdx = text.indexOf(" - https://", 0, Qt::CaseInsensitive);
  if (httpsIdx >= 0)
  {
    return text.mid(httpsIdx + 3).trimmed().toStdString();
  }

  return text.toStdString();
}

void MainForm::SetEsclScanningUiState(bool scanning)
{
  m_bEsclScanning = scanning;
  bool enabled = !scanning;

  ui->pPushButton_AddFiles->setEnabled(enabled);
  ui->pListWidget_Files->setEnabled(enabled);
  ui->pComboBox_EsclUrl->setEnabled(enabled);
  ui->pComboBox_EsclSource->setEnabled(enabled);
  ui->pComboBox_EsclPaper->setEnabled(enabled);
  ui->pCheckBox_EsclAutoSend->setEnabled(enabled);
  ui->pPushButton_EsclScan->setEnabled(enabled);
  ui->pGroupBox_ScanSettings->setEnabled(enabled);
  ui->pPushButton_Scan->setEnabled(enabled);

  ui->pPushButton_EsclScan->setText(scanning ? "Scanning..." : "Scan from Network");
  ui->pPushButton_Cancel->setText(scanning ? "Cancel Scan" : "Cancel");
  ui->pPushButton_Cancel->setEnabled(true);

  if (scanning)
  {
    setCursor(Qt::WaitCursor);
  }
  else
  {
    unsetCursor();
  }

  UpdateQueueSummary();
}

void MainForm::StopEsclWorker()
{
  if (m_esclCancel)
  {
    m_esclCancel->cancel();
  }
  if (m_esclThread.joinable())
  {
    m_esclThread.join();
  }
  m_esclCancel.reset();
  m_bEsclScanning = false;
}

void MainForm::HandleEsclScanFinished(const EsclScanResult& result)
{
  if (m_esclThread.joinable())
  {
    m_esclThread.join();
  }
  m_esclCancel.reset();
  SetEsclScanningUiState(false);

  if (result.cancelled)
  {
    ui->pLabel_QueueSummary->setText("Network scan cancelled.");
    BringDialogToFront();
    return;
  }

  if (!result.ok)
  {
    UpdateQueueSummary();
    QMessageBox::warning(this, "Virtual Scanner", QString::fromStdString(result.error));
    BringDialogToFront();
    return;
  }

  CEsclClient::upsertScanner(m_esclSettings, result.makeAndModel, result.resolvedUrl);
  CEsclClient::saveSettings(m_esclSettings);
  RefreshEsclCombo();

  bool addedAny = false;
  for (size_t i = 0; i < result.savedFiles.size(); ++i)
  {
    bool isPdf = false;
    bool isMultiTiff = false;
    int pageCount = CScanner_FreeImage::getFilePageCount(result.savedFiles[i], isPdf, isMultiTiff);
    if (pageCount <= 0)
    {
      continue;
    }

    SelectedFileEntry entry;
    entry.filePath    = result.savedFiles[i];
    entry.pageCount   = pageCount;
    entry.isPdf       = isPdf;
    entry.isMultiTiff = isMultiTiff;
    m_files.push_back(entry);
    addedAny = true;
  }

  RefreshFileList();

  if (!addedAny)
  {
    QMessageBox::warning(this, "Virtual Scanner", "The scanned image could not be loaded.");
    BringDialogToFront();
    return;
  }

  if (ui->pCheckBox_EsclAutoSend->isChecked())
  {
    on_pPushButton_Scan_clicked(false);
  }
  else
  {
    BringDialogToFront();
  }
}

void MainForm::on_pPushButton_EsclScan_clicked(bool _bChecked)
{
  (void)_bChecked;
  if (m_bEsclScanning)
  {
    return;
  }

  std::string target = GetSelectedEsclTarget();
  if (target.empty())
  {
    QMessageBox::warning(this, "Virtual Scanner",
                         "Enter a scanner IP address or eSCL URL (e.g. 192.168.1.100).");
    ui->pComboBox_EsclUrl->setFocus();
    return;
  }

  EsclScanRequest req;
  req.target = target;
  req.source = (EsclInputSource)ui->pComboBox_EsclSource->currentIndex();
  req.paper  = (EsclPaperSize)ui->pComboBox_EsclPaper->currentIndex();
  req.dpi    = (ui->pComboBox_Resolution->currentIndex() >= 0)
    ? ui->pComboBox_Resolution->currentData().toInt()
    : 200;
  req.pixelType = (ui->pComboBox_ColorMode->currentIndex() >= 0)
    ? ui->pComboBox_ColorMode->currentData().toInt()
    : TWPT_RGB;

  if (m_esclThread.joinable())
  {
    m_esclThread.join();
  }

  std::shared_ptr<EsclCancelToken> cancelToken = std::make_shared<EsclCancelToken>();
  m_esclCancel = cancelToken;

  SetEsclScanningUiState(true);
  ui->pLabel_QueueSummary->setText(QString("Connecting to %1...").arg(QString::fromStdString(target)));

  m_esclThread = std::thread([this, req, cancelToken]() {
    EsclScanResult result = CEsclClient::scan(req, cancelToken, [this](const std::string& msg) {
      QString qMsg = QString::fromStdString(msg);
      QMetaObject::invokeMethod(this, [this, qMsg]() {
        if (m_bEsclScanning)
        {
          ui->pLabel_QueueSummary->setText(qMsg);
        }
      }, Qt::QueuedConnection);
    });

    QMetaObject::invokeMethod(this, [this, result]() {
      HandleEsclScanFinished(result);
    }, Qt::QueuedConnection);
  });
}

void MainForm::on_pComboBox_EsclSource_currentIndexChanged(int _nIndex)
{
  if (m_bEsclInitializing || _nIndex < 0)
  {
    return;
  }
  m_esclSettings.lastSource = _nIndex;
  CEsclClient::saveSettings(m_esclSettings);
}

void MainForm::on_pComboBox_EsclPaper_currentIndexChanged(int _nIndex)
{
  if (m_bEsclInitializing || _nIndex < 0)
  {
    return;
  }
  m_esclSettings.lastPaper = _nIndex;
  CEsclClient::saveSettings(m_esclSettings);
}

void MainForm::on_pCheckBox_EsclAutoSend_toggled(bool _bChecked)
{
  if (m_bEsclInitializing)
  {
    return;
  }
  m_esclSettings.autoSend = _bChecked;
  CEsclClient::saveSettings(m_esclSettings);
}

void MainForm::on_pPushButton_AddFiles_clicked(bool _bChecked)
{
  (void)_bChecked;
  OpenNativeMultiFilePicker();
}

void MainForm::on_pPushButton_MoveUp_clicked(bool _bChecked)
{
  (void)_bChecked;
  int row = ui->pListWidget_Files->currentRow();
  if (row > 0 && row < (int)m_files.size())
  {
    std::swap(m_files[(size_t)row], m_files[(size_t)(row - 1)]);
    RefreshFileList();
    ui->pListWidget_Files->setCurrentRow(row - 1);
  }
}

void MainForm::on_pPushButton_MoveDown_clicked(bool _bChecked)
{
  (void)_bChecked;
  int row = ui->pListWidget_Files->currentRow();
  if (row >= 0 && row + 1 < (int)m_files.size())
  {
    std::swap(m_files[(size_t)row], m_files[(size_t)(row + 1)]);
    RefreshFileList();
    ui->pListWidget_Files->setCurrentRow(row + 1);
  }
}

void MainForm::on_pPushButton_RemoveFile_clicked(bool _bChecked)
{
  (void)_bChecked;
  QList<QListWidgetItem*> selected = ui->pListWidget_Files->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }
  std::vector<int> rows;
  for (int i = 0; i < selected.size(); ++i)
  {
    rows.push_back(ui->pListWidget_Files->row(selected[i]));
  }
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (size_t i = 0; i < rows.size(); ++i)
  {
    int r = rows[i];
    if (r >= 0 && r < (int)m_files.size())
    {
      m_files.erase(m_files.begin() + r);
    }
  }
  RefreshFileList();
}

void MainForm::on_pPushButton_ClearFiles_clicked(bool _bChecked)
{
  (void)_bChecked;
  m_files.clear();
  RefreshFileList();
}

void MainForm::on_pComboBox_Resolution_activated(int _nIndex)
{
  if (m_pModalRequest || !m_pUI)
  {
    return;
  }
  QVariant val = ui->pComboBox_Resolution->itemData(_nIndex);
  m_pUI->SetCapValueFloat(ICAP_XRESOLUTION, (float)val.toInt());
  m_pUI->SetCapValueFloat(ICAP_YRESOLUTION, (float)val.toInt());
  UpdateControls();
}

void MainForm::on_pComboBox_ColorMode_activated(int _nIndex)
{
  if (m_pModalRequest || !m_pUI)
  {
    return;
  }
  QVariant val = ui->pComboBox_ColorMode->itemData(_nIndex);
  m_pUI->SetCapValueInt(ICAP_PIXELTYPE, val.toInt());
  UpdateControls();
}

void MainForm::on_pComboBox_MultiPageMode_currentIndexChanged(int _nIndex)
{
  bool isCustom = (_nIndex == kModeCustomRange);
  ui->pLabel_PageRange->setEnabled(isCustom);
  ui->pLineEdit_PageRange->setEnabled(isCustom);
  UpdateQueueSummary();
}

void MainForm::on_pLineEdit_PageRange_textChanged(const QString &text)
{
  (void)text;
  UpdateQueueSummary();
}

void MainForm::on_pPushButton_Scan_clicked(bool _bChecked)
{
  (void)_bChecked;
  if (m_bEsclScanning)
  {
    return;
  }

  if (!m_pModalRequest && m_pUI && m_pUI->m_bSetup)
  {
    m_bScanStarted = true;
    m_pUI->Save();
    return;
  }

  if (m_files.empty())
  {
    if (!OpenNativeMultiFilePicker())
    {
      return;
    }
  }

  std::vector<ScanPageItem> queuedPages;
  QString err;
  if (!BuildQueuedPages(queuedPages, err))
  {
    QMessageBox::warning(this, "Virtual Scanner", err);
    return;
  }

  if (m_pModalRequest)
  {
    m_pModalRequest->selectedPages = queuedPages;
    if (ui->pComboBox_Resolution->currentIndex() >= 0)
    {
      m_pModalRequest->selectedDpi = (float)ui->pComboBox_Resolution->currentData().toInt();
    }
    if (ui->pComboBox_ColorMode->currentIndex() >= 0)
    {
      m_pModalRequest->selectedPixelType = ui->pComboBox_ColorMode->currentData().toInt();
    }
    m_pModalRequest->accepted = true;
    m_bScanStarted = true;
    accept();
    return;
  }

  if (!m_pUI)
  {
    return;
  }

  // Ensure ADF is enabled when transferring multiple pages
  m_pUI->SetCapValueInt(CAP_FEEDERENABLED, (queuedPages.size() > 1) ? 1 : 0);
  m_pUI->m_pDS->SetSelectedPages(queuedPages);

  m_bScanStarted = true;
  ui->pPushButton_Scan->setEnabled(false);
  ui->pPushButton_AddFiles->setEnabled(false);
  ui->pPushButton_EsclScan->setEnabled(false);
  m_pUI->Scan();

  if (!m_pUI->m_bScanning)
  {
    m_bScanStarted = false;
    ui->pPushButton_Scan->setEnabled(true);
    ui->pPushButton_AddFiles->setEnabled(true);
    ui->pPushButton_EsclScan->setEnabled(true);
    QMessageBox::warning(this, "Virtual Scanner", "Failed to prepare the selected page for scanning.");
  }
}

void MainForm::on_pPushButton_Cancel_clicked(bool _bChecked)
{
  (void)_bChecked;
  if (m_bEsclScanning)
  {
    if (m_esclCancel)
    {
      m_esclCancel->cancel();
    }
    ui->pLabel_QueueSummary->setText("Cancelling network scan...");
    ui->pPushButton_Cancel->setEnabled(false);
    return;
  }

  if (m_pModalRequest)
  {
    m_pModalRequest->accepted = false;
    reject();
    return;
  }
  QEvent *pQEvent = new QEvent(QEvent::Close);
  qApp->postEvent(this, pQEvent);
}

void MainForm::closeEvent(QCloseEvent *evClose)
{
  StopEsclWorker();

  if (m_pModalRequest)
  {
    if (!m_bScanStarted)
    {
      m_pModalRequest->accepted = false;
    }
    evClose->accept();
    return;
  }

  if (!m_bScanStarted && m_pUI)
  {
    m_pUI->m_pDS->ClearSelectedPages();
    m_pUI->Cancel();
  }
  evClose->setAccepted(false);
}

void MainForm::hideEvent(QHideEvent *evHide)
{
  QDialog::hideEvent(evHide);
}

void MainForm::PopulateModalControls()
{
  ui->pComboBox_ColorMode->clear();
  if (m_pModalRequest && !m_pModalRequest->validPixelTypes.empty())
  {
    for (unsigned int i = 0; i < m_pModalRequest->validPixelTypes.size(); i++)
    {
      int val = m_pModalRequest->validPixelTypes.at(i);
      switch (val)
      {
        case TWPT_BW:
          ui->pComboBox_ColorMode->addItem("Black and White", val);
          break;
        case TWPT_GRAY:
          ui->pComboBox_ColorMode->addItem("Grayscale", val);
          break;
        case TWPT_RGB:
          ui->pComboBox_ColorMode->addItem("Color", val);
          break;
        default:
          continue;
      }
      if ((int)i == m_pModalRequest->currentPixelTypeIndex)
      {
        ui->pComboBox_ColorMode->setCurrentIndex(ui->pComboBox_ColorMode->count() - 1);
      }
    }
  }
  else
  {
    ui->pComboBox_ColorMode->addItem("Black and White", TWPT_BW);
    ui->pComboBox_ColorMode->addItem("Grayscale", TWPT_GRAY);
    ui->pComboBox_ColorMode->addItem("Color", TWPT_RGB);
    ui->pComboBox_ColorMode->setCurrentIndex(2);
  }

  ui->pComboBox_Resolution->clear();
  if (m_pModalRequest && !m_pModalRequest->validResolutions.empty())
  {
    for (unsigned int i = 0; i < m_pModalRequest->validResolutions.size(); i++)
    {
      int dpi = (int)m_pModalRequest->validResolutions.at(i);
      ui->pComboBox_Resolution->addItem(QString("%1 DPI").arg(dpi), dpi);
      if ((int)i == m_pModalRequest->currentResolutionIndex)
      {
        ui->pComboBox_Resolution->setCurrentIndex(ui->pComboBox_Resolution->count() - 1);
      }
    }
  }
  else
  {
    const int defaultDpis[] = { 100, 150, 200, 300, 600 };
    for (size_t i = 0; i < sizeof(defaultDpis) / sizeof(defaultDpis[0]); ++i)
    {
      ui->pComboBox_Resolution->addItem(QString("%1 DPI").arg(defaultDpis[i]), defaultDpis[i]);
    }
    ui->pComboBox_Resolution->setCurrentIndex(2); // 200 DPI
  }
}

void MainForm::UpdateControls()
{
  if (!m_pUI)
  {
    return;
  }
  ui->pComboBox_ColorMode->clear();
  int nCapIndex = m_pUI->GetCurrentCapIndex(ICAP_PIXELTYPE);
  const IntVector* lstCapValues = m_pUI->GetValidCap(ICAP_PIXELTYPE);
  if (lstCapValues)
  {
    for (unsigned int i = 0; i < lstCapValues->size(); i++)
    {
      switch (lstCapValues->at(i))
      {
        case TWPT_BW:
          ui->pComboBox_ColorMode->addItem("Black and White", lstCapValues->at(i));
          break;
        case TWPT_GRAY:
          ui->pComboBox_ColorMode->addItem("Grayscale", lstCapValues->at(i));
          break;
        case TWPT_RGB:
          ui->pComboBox_ColorMode->addItem("Color", lstCapValues->at(i));
          break;
        default:
          continue;
      }
      if ((int)i == nCapIndex)
      {
        ui->pComboBox_ColorMode->setCurrentIndex(ui->pComboBox_ColorMode->count() - 1);
      }
    }
  }

  ui->pComboBox_Resolution->clear();
  nCapIndex = m_pUI->GetCurrentCapIndex(ICAP_XRESOLUTION);
  const FloatVector *lstCapValuesFlt = m_pUI->GetValidCapFloat(ICAP_XRESOLUTION);
  if (lstCapValuesFlt)
  {
    for (unsigned int i = 0; i < lstCapValuesFlt->size(); i++)
    {
      int dpi = (int)lstCapValuesFlt->at(i);
      ui->pComboBox_Resolution->addItem(QString("%1 DPI").arg(dpi), dpi);
      if ((int)i == nCapIndex)
      {
        ui->pComboBox_Resolution->setCurrentIndex(ui->pComboBox_Resolution->count() - 1);
      }
    }
  }
}
