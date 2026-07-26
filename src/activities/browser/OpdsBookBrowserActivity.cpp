#include "OpdsBookBrowserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdint>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr size_t OPDS_BROWSER_ENTRY_CAPACITY = MAX_OPDS_FEED_ENTRIES + 2;
constexpr size_t OPDS_CATALOG_DOWNLOAD_BUFFER_SIZE = 512;
constexpr size_t OPDS_DOWNLOAD_BUFFER_SIZE = 2048;
constexpr uint8_t OPDS_DOWNLOAD_PROGRESS_STEP_PERCENT = 25;
constexpr char FEED_TMP_PATH[] = "/.crosspoint/opds_feed.tmp";

std::string buildBookFilenameBase(const OpdsEntry& book, const OpdsFilenameFormat format) {
  if (book.author.empty()) return book.title;
  if (book.title.empty()) return book.author;
  if (format == OpdsFilenameFormat::TITLE_AUTHOR) return book.title + " - " + book.author;
  return book.author + " - " + book.title;
}
}  // namespace

void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();

  sdFontSystem.releaseLoadedFont(renderer);

  state = BrowserState::CHECK_WIFI;
  entryCount = 0;
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = "";
  selectorIndex = 0;
  consumeConfirm = false;
  consumeBack = false;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  if (!ensureEntryBuffer()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }

  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  Activity::onExit();
  clearEntries();
  entries.reset();
  navigationHistory.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OpdsBookBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) {
    return;
  }

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }
  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        showLoadingBeforeFetch();
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state == BrowserState::CHECK_WIFI ? onGoHome() : navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) return;

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (entryCount > 0) {
        const auto& entry = entries[selectorIndex];
        entry.type == OpdsEntryType::BOOK ? downloadBook(entry) : navigateToEntry(entry);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!searchTemplate.empty() && selectorIndex == 0) launchSearch();
    }

    if (entryCount > 0) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entryCount);
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entryCount);
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entryCount, PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entryCount, PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

bool OpdsBookBrowserActivity::preventAutoSleep() {
  switch (state) {
    case BrowserState::CHECK_WIFI:
    case BrowserState::WIFI_SELECTION:
    case BrowserState::LOADING:
    case BrowserState::DOWNLOADING:
    case BrowserState::SEARCH_INPUT:
      return true;
    case BrowserState::BROWSING:
    case BrowserState::ERROR:
      return false;
  }
  return false;
}

void OpdsBookBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Show server name in header if available, otherwise generic title
  const char* headerTitle = server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const char* confirmLabel =
      (entryCount > 0 && entries[selectorIndex].type == OpdsEntryType::BOOK) ? tr(STR_DOWNLOAD) : tr(STR_OPEN);
  const char* searchLabel = (!searchTemplate.empty() && selectorIndex == 0) ? tr(STR_SEARCH) : tr(STR_DIR_UP);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, searchLabel, tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entryCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = pageStartIndex; i < entryCount && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& entry = entries[i];
      std::string displayText = (entry.type == OpdsEntryType::NAVIGATION) ? "> " + entry.title : entry.title;
      if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) displayText += " - " + entry.author;
      auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void OpdsBookBrowserActivity::showLoadingBeforeFetch() {
  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("OPDS", "Loading screen could not be rendered before feed fetch");
    requestUpdate(true);
  }
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  clearEntries();
  // Parsing the previous feed fragments the heap around this fixed array.
  // Destroy it before opening the next TLS connection, then allocate a fresh
  // entry buffer only after the network client and SSL buffers are gone.
  entries.reset();
  std::string{}.swap(searchTemplate);
  std::string url = (path.find("http") == 0) ? path : UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());
  // Spool the feed to SD and parse it only after the connection is closed.
  // Parsing while the socket is open holds the whole TLS session (~35KB of
  // heap with 16KB SSL buffers) while the parser allocates entry strings
  // into what remains, so large feeds over HTTPS can run out of memory
  // mid-transfer. Downloading first, then constructing the parser and
  // reading from SD, keeps the transfer phase free of parse allocations.
  static constexpr const char* FEED_TMP_PATH = "/.crosspoint/opds-feed.tmp";
  bool cancelRequested = false;
  auto pollCancel = [this, &cancelRequested] {
    if (cancelRequested) {
      return true;
    }
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    return cancelRequested;
  };
  HttpDownloader::DownloadOptions feedOptions;
  feedOptions.shouldCancel = pollCancel;
  feedOptions.bufferSize = OPDS_CATALOG_DOWNLOAD_BUFFER_SIZE;

  const HttpDownloader::DownloadError feedErr = HttpDownloader::downloadToFile(
      url, FEED_TMP_PATH, nullptr, &cancelRequested, server.username, server.password, feedOptions);
  if (feedErr == HttpDownloader::ABORTED) {
    // User-initiated cancel: go back instead of showing an error screen,
    // matching what loop() does for Back while LOADING. downloadToFile
    // already removed the temp file.
    LOG_DBG("OPDS", "Feed fetch cancelled");
    mappedInput.suppressNextBackRelease();
    navigateBack();
    return;
  }
  if (feedErr != HttpDownloader::OK) {
    LOG_ERR("OPDS", "Feed download failed: %d", static_cast<int>(feedErr));
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  if (!ensureEntryBuffer()) {
    Storage.remove(FEED_TMP_PATH);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }

  FsFile feedFile;
  if (!Storage.openFileForRead("OPDS", FEED_TMP_PATH, feedFile)) {
    LOG_ERR("OPDS", "Failed to open spooled feed %s", FEED_TMP_PATH);
    Storage.remove(FEED_TMP_PATH);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(OPDS_DOWNLOAD_BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("OPDS", "OOM: feed read buffer (%u free, %u max alloc)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    feedFile.close();
    Storage.remove(FEED_TMP_PATH);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }

  OpdsParser parser(entries.get(), MAX_OPDS_FEED_ENTRIES);
  bool readOk = true;
  {
    OpdsParserStream stream{parser};
    while (parser) {
      const int bytesRead = feedFile.read(buffer.get(), OPDS_DOWNLOAD_BUFFER_SIZE);
      if (bytesRead < 0) readOk = false;
      if (bytesRead <= 0) break;
      stream.write(buffer.get(), static_cast<size_t>(bytesRead));
    }
  }
  feedFile.close();
  Storage.remove(FEED_TMP_PATH);
  if (!readOk) {
    LOG_ERR("OPDS", "Failed to read spooled feed %s", FEED_TMP_PATH);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = parser.getErrorReason() == OpdsParserError::BUFFER_MEMORY ? tr(STR_OPDS_FEED_BUFFER_MEMORY_ERROR)
                                                                             : tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  entryCount = parser.getEntryCount();
  if (parser.wasTruncated()) {
    LOG_DBG("OPDS", "Feed truncated to %zu entries", entryCount);
  }

  if (!prevUrl.empty()) {
    for (size_t i = entryCount; i > 0; --i) {
      entries[i] = std::move(entries[i - 1]);
    }
    entries[0] = OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""};
    entryCount++;
  }
  if (!nextUrl.empty() && !appendEntry(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""})) {
    LOG_DBG("OPDS", "No room for next-page entry");
  }

  selectorIndex = 0;
  state = entryCount == 0 ? BrowserState::ERROR : BrowserState::BROWSING;
  if (entryCount == 0) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

bool OpdsBookBrowserActivity::ensureEntryBuffer() {
  if (entries) return true;
  entries = makeUniqueNoThrow<OpdsEntry[]>(OPDS_BROWSER_ENTRY_CAPACITY);
  return entries != nullptr;
}

void OpdsBookBrowserActivity::clearEntries() {
  // Release strings from the previous feed before opening the next HTTPS
  // connection. Merely resetting entryCount leaves title/href/author
  // allocations alive in every slot, reducing the contiguous heap available
  // to TLS and causing nested feeds such as Calibre-Web Authors to fail.
  if (entries) {
    for (size_t i = 0; i < entryCount; ++i) {
      entries[i] = OpdsEntry{};
    }
  }
  entryCount = 0;
}

bool OpdsBookBrowserActivity::appendEntry(OpdsEntry&& entry) {
  if (!entries || entryCount >= OPDS_BROWSER_ENTRY_CAPACITY) return false;
  entries[entryCount++] = std::move(entry);
  return true;
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  clearEntries();
  selectorIndex = 0;
  showLoadingBeforeFetch();
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    clearEntries();
    selectorIndex = 0;
    showLoadingBeforeFetch();
    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = downloadTotal = 0;
  lastDownloadRenderedPercent = 0;
  requestUpdate(true);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  std::string filename =
      "/" + StringUtils::sanitizeFilename(buildBookFilenameBase(book, server.filenameFormat)) + ".epub";
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  // The selected book data is now copied into downloadUrl, filename, and
  // statusMessage. Release the catalog before opening another TLS connection;
  // keeping up to 50 entries of titles, authors, IDs, and URLs alive here can
  // leave too little contiguous heap for an EPUB download on ESP32-C3.
  clearEntries();
  entries.reset();
  std::string{}.swap(searchTemplate);

  bool cancelRequested = false;
  auto pollCancel = [this, &cancelRequested] {
    if (cancelRequested) {
      return true;
    }
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    return cancelRequested;
  };
  HttpDownloader::DownloadOptions downloadOptions;
  downloadOptions.shouldCancel = pollCancel;
  downloadOptions.bufferSize = OPDS_DOWNLOAD_BUFFER_SIZE;

  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        if (total == 0) return;

        const auto percent = static_cast<uint8_t>(
            std::min<uint64_t>(100, (static_cast<uint64_t>(downloaded) * 100) / static_cast<uint64_t>(total)));
        if (percent == 100 || percent >= lastDownloadRenderedPercent + OPDS_DOWNLOAD_PROGRESS_STEP_PERCENT) {
          lastDownloadRenderedPercent = percent;
          requestUpdate(true);
        }
      },
      &cancelRequested, server.username, server.password, downloadOptions);

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    showLoadingBeforeFetch();
    fetchFeed(currentPath);
    return;
  } else if (result == HttpDownloader::ABORTED) {
    LOG_DBG("OPDS", "Download cancelled");
    mappedInput.suppressNextBackRelease();
    showLoadingBeforeFetch();
    fetchFeed(currentPath);
    return;
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
  }
  requestUpdate();
}

void OpdsBookBrowserActivity::launchSearch() {
  consumeConfirm = true;
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      performSearch(std::get<KeyboardResult>(result.data).text);
    } else {
      requestUpdate();
    }
  });
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  navigationHistory.push_back(currentPath);  // <-- add this
  currentPath = url;                         // <-- add this

  showLoadingBeforeFetch();
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    showLoadingBeforeFetch();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void OpdsBookBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    showLoadingBeforeFetch();
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
