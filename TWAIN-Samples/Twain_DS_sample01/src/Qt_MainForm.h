#ifndef QT_MAINFORM_H
#define QT_MAINFORM_H
#include "QT_UI.h"
#include "CEsclClient.h"

#include <QtWidgets/QDialog>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Ui
{
  class MainForm;
}

class QShowEvent;

class MainForm : public QDialog
{
  Q_OBJECT

public:
  MainForm(QT_UI *pUI, QWidget *parent = 0);
  explicit MainForm(ModalForceUIRequest *pModalRequest, QWidget *parent = 0);
  ~MainForm();

protected slots:
  // File queue buttons
  void on_pPushButton_AddFiles_clicked(bool _bChecked = false);
  void on_pPushButton_MoveUp_clicked(bool _bChecked = false);
  void on_pPushButton_MoveDown_clicked(bool _bChecked = false);
  void on_pPushButton_RemoveFile_clicked(bool _bChecked = false);
  void on_pPushButton_ClearFiles_clicked(bool _bChecked = false);

  // Network eSCL scanner controls
  void on_pPushButton_EsclScan_clicked(bool _bChecked = false);
  void on_pComboBox_EsclSource_currentIndexChanged(int _nIndex);
  void on_pComboBox_EsclPaper_currentIndexChanged(int _nIndex);
  void on_pCheckBox_EsclAutoSend_toggled(bool _bChecked);

  // Scan settings controls
  void on_pComboBox_Resolution_activated(int _nIndex);
  void on_pComboBox_ColorMode_activated(int _nIndex);
  void on_pComboBox_MultiPageMode_currentIndexChanged(int _nIndex);
  void on_pLineEdit_PageRange_textChanged(const QString &text);

  // Bottom buttons
  void on_pPushButton_Scan_clicked(bool _bChecked = false);
  void on_pPushButton_Cancel_clicked(bool _bChecked = false);

private:
  struct SelectedFileEntry
  {
    std::wstring filePath;
    int          pageCount;
    bool         isPdf;
    bool         isMultiTiff;
  };

  Ui::MainForm *ui;
  QT_UI *m_pUI;
  ModalForceUIRequest *m_pModalRequest;
  std::vector<SelectedFileEntry> m_files;
  bool m_bScanStarted;
  bool m_bFirstShow;

  EsclUserSettings m_esclSettings;
  bool m_bEsclInitializing;
  bool m_bEsclScanning;
  std::shared_ptr<EsclCancelToken> m_esclCancel;
  std::thread m_esclThread;

  void UpdateControls(void);
  void PopulateModalControls(void);
  void InitEsclControls(void);
  void RefreshEsclCombo(void);
  std::string GetSelectedEsclTarget(void) const;
  void SetEsclScanningUiState(bool scanning);
  void StopEsclWorker(void);
  void HandleEsclScanFinished(const EsclScanResult& result);
  void RefreshFileList(void);
  void UpdateQueueSummary(void);
  void BringDialogToFront(void);
  bool OpenNativeMultiFilePicker(void);
  bool BuildQueuedPages(std::vector<ScanPageItem>& outPages, QString& outError) const;
  static bool ParsePageRange(const QString& rangeText, int totalPages, std::vector<int>& outZeroBasedIndices);

  void showEvent(QShowEvent *evShow);
  void closeEvent(QCloseEvent *);
  void hideEvent(QHideEvent *evHide);
};

#endif // QT_MAINFORM_H
