#ifndef CESCLCLIENT_H
#define CESCLCLIENT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum EsclInputSource
{
  ESCL_SOURCE_PLATEN      = 0,
  ESCL_SOURCE_ADF_SIMPLEX = 1,
  ESCL_SOURCE_ADF_DUPLEX  = 2
};

enum EsclPaperSize
{
  ESCL_PAPER_A4        = 0,
  ESCL_PAPER_A5        = 1,
  ESCL_PAPER_LETTER    = 2,
  ESCL_PAPER_MAX_GLASS = 3
};

struct EsclScannerEntry
{
  std::string name;
  std::string url;
};

struct EsclUserSettings
{
  std::vector<EsclScannerEntry> scanners;
  int  lastSource;
  int  lastPaper;
  bool autoSend;

  EsclUserSettings()
    : lastSource(ESCL_SOURCE_PLATEN)
    , lastPaper(ESCL_PAPER_A4)
    , autoSend(true)
  {
  }
};

struct EsclScanRequest
{
  std::string     target;
  EsclInputSource source;
  EsclPaperSize   paper;
  int             dpi;
  int             pixelType; // TWPT_BW (0), TWPT_GRAY (1), TWPT_RGB (2)

  EsclScanRequest()
    : source(ESCL_SOURCE_PLATEN)
    , paper(ESCL_PAPER_A4)
    , dpi(200)
    , pixelType(2)
  {
  }
};

struct EsclScanResult
{
  bool                       ok;
  bool                       cancelled;
  std::string                resolvedUrl;
  std::string                makeAndModel;
  std::vector<std::wstring>  savedFiles;
  std::string                error;

  EsclScanResult()
    : ok(false)
    , cancelled(false)
  {
  }
};

class EsclCancelToken
{
public:
  EsclCancelToken();
  ~EsclCancelToken();

  void cancel();
  bool isCancelled() const;

  bool setActiveHandle(void* hInternet);
  bool clearActiveHandle(void* hInternet);

private:
  std::atomic<bool> m_cancelled;
  std::mutex        m_mutex;
  void*             m_activeHandle;
};

class CEsclClient
{
public:
  typedef std::function<void(const std::string&)> ProgressCallback;

  // Loads saved eSCL scanner entries and preferences (merges source.json defaults
  // with %LOCALAPPDATA%\VirtualScanner\escl_settings.json).
  static EsclUserSettings loadSettings();

  // Saves user eSCL scanner entries and preferences into %LOCALAPPDATA%\VirtualScanner\escl_settings.json.
  static void saveSettings(const EsclUserSettings& settings);

  // Inserts or updates a scanner entry at the top of the list (most recently used first).
  static void upsertScanner(EsclUserSettings& settings, const std::string& name, const std::string& url);

  // Performs a synchronous eSCL scan (call from a background thread).
  static EsclScanResult scan(
    const EsclScanRequest& request,
    const std::shared_ptr<EsclCancelToken>& cancelToken,
    ProgressCallback onProgress = ProgressCallback());

  // Removes old temporary eSCL scan files (> 24 hours old).
  static void cleanupOldTempFiles();
};

#endif // CESCLCLIENT_H
