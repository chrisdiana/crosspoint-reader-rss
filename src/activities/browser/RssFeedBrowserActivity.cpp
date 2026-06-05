#include "RssFeedBrowserActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HtmlArticleExtractor.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/RssArticleActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr int LIST_FONT_ID = UI_12_FONT_ID;
constexpr int LIST_TOP = 60;
constexpr int ROW_HEIGHT = 38;
constexpr int PAGE_ITEMS = 18;
constexpr size_t MAX_ARTICLE_HTML_BYTES = 24 * 1024;
constexpr char ARTICLE_TMP_DIR[] = "/.crosspoint";
constexpr char ARTICLE_TMP_FILE[] = "/.crosspoint/rss_article.tmp";

std::string wikipediaRenderUrl(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  if (pathStart == std::string::npos) return {};

  const std::string host = url.substr(hostStart, pathStart - hostStart);
  if (host.find("wikipedia.org") == std::string::npos) return {};
  if (url.compare(pathStart, 6, "/wiki/") != 0) return {};

  size_t titleEnd = url.find_first_of("?#", pathStart + 6);
  if (titleEnd == std::string::npos) titleEnd = url.size();
  if (titleEnd <= pathStart + 6) return {};

  const std::string title = url.substr(pathStart + 6, titleEnd - pathStart - 6);
  return url.substr(0, schemeEnd + 3) + host + "/w/index.php?title=" + title + "&action=render";
}
}

void RssFeedBrowserActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  items.clear();
  feedTitle.clear();
  selectorIndex = 0;
  consumeBack = false;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void RssFeedBrowserActivity::onExit() {
  Activity::onExit();
  items.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void RssFeedBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) return;

  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::RSS_READER);
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onGoHome(HomeMenuItem::RSS_READER);
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!items.empty()) openItem(items[selectorIndex]);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::RSS_READER);
    }

    if (!items.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void RssFeedBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* headerTitle =
      !feed.name.empty() ? feed.name.c_str() : (!feedTitle.empty() ? feedTitle.c_str() : tr(STR_RSS_READER));
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
    if (state == BrowserState::ARTICLE_LOADING) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_LOADING));
      auto row = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, row.c_str());
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    }
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

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (items.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, LIST_TOP + (selectorIndex % PAGE_ITEMS) * ROW_HEIGHT - 3, pageWidth - 1, ROW_HEIGHT);

    for (size_t i = pageStartIndex; i < items.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& item = items[i];
      std::string displayText = item.title;
      if (!item.published.empty()) displayText += " - " + item.published;
      auto row = renderer.truncatedText(LIST_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(LIST_FONT_ID, 20, LIST_TOP + (i % PAGE_ITEMS) * ROW_HEIGHT, row.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void RssFeedBrowserActivity::fetchFeed() {
  if (feed.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_FEED_URL);
    requestUpdate();
    return;
  }

  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());
  RssParser parser;
  if (!HttpDownloader::fetchUrl(
          feed.url, [&parser](const uint8_t* data, size_t len) { return parser.write(data, len) == len; },
          feed.username, feed.password)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }
  parser.flush();

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  feedTitle = parser.getFeedTitle();
  items = std::move(parser).getItems();
  selectorIndex = 0;
  state = items.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (items.empty()) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

void RssFeedBrowserActivity::openItem(const RssItem& item) {
  consumeBack = true;
  state = BrowserState::ARTICLE_LOADING;
  statusMessage = item.title;
  // Force the loading screen to paint before the blocking article fetch starts.
  // fetchArticleText() clears font caches afterward so TLS still starts with
  // the largest heap block we can give it.
  requestUpdateAndWait();

  RssItem article = item;
  std::string extracted = fetchArticleText(item);
  if (!extracted.empty()) {
    LOG_DBG("RSS", "Using extracted article text (%zu bytes): %s", extracted.size(), item.link.c_str());
    article.content = std::move(extracted);
  } else {
    LOG_DBG("RSS", "Using feed summary fallback (%zu bytes): %s", item.content.size(), item.link.c_str());
  }

  state = BrowserState::BROWSING;
  startActivityForResult(std::make_unique<RssArticleActivity>(renderer, mappedInput, article),
                         [this](const ActivityResult&) { requestUpdate(); });
}

std::string RssFeedBrowserActivity::fetchArticleText(const RssItem& item) {
  if (item.link.find("http://") != 0 && item.link.find("https://") != 0) {
    LOG_DBG("RSS", "Article link is not fetchable: %s", item.link.c_str());
    return {};
  }

  auto* fontCache = renderer.getFontCacheManager();
  if (fontCache) fontCache->clearCache();
  LOG_DBG("RSS", "Article heap before fetch: free=%u max=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (!Storage.exists(ARTICLE_TMP_DIR)) Storage.ensureDirectoryExists(ARTICLE_TMP_DIR);

  auto fetchHtmlToFile = [&](const std::string& url) {
    Storage.remove(ARTICLE_TMP_FILE);
    HalFile file;
    if (!Storage.openFileForWrite("RSS", ARTICLE_TMP_FILE, file)) {
      LOG_ERR("RSS", "Failed to open article temp file");
      return false;
    }

    bool reachedLimit = false;
    const bool fetched = HttpDownloader::fetchUrl(
        url,
        [&file, &reachedLimit](const uint8_t* data, size_t len) {
          const size_t currentSize = file.size();
          const size_t remaining = MAX_ARTICLE_HTML_BYTES > currentSize ? MAX_ARTICLE_HTML_BYTES - currentSize : 0;
          if (remaining == 0) {
            reachedLimit = true;
            return false;
          }
          const size_t copyLen = std::min(remaining, len);
          if (file.write(data, copyLen) != copyLen) return false;
          if (copyLen < len) {
            reachedLimit = true;
            return false;
          }
          return true;
        },
        feed.username, feed.password);
    const size_t storedBytes = file.size();
    file.close();
    if (reachedLimit) LOG_DBG("RSS", "Article HTML truncated: %s", url.c_str());
    return fetched || storedBytes > 0;
  };

  auto fetchAndExtract = [&](const std::string& url) {
    bool fetched = fetchHtmlToFile(url);
    size_t htmlBytes = Storage.exists(ARTICLE_TMP_FILE) ? Storage.open(ARTICLE_TMP_FILE).size() : 0;
    if (!fetched && htmlBytes == 0) {
      LOG_DBG("RSS", "Article empty fetch failed; retrying after cache clear: free=%u max=%u", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      if (fontCache) fontCache->clearCache();
      delay(50);
      fetched = fetchHtmlToFile(url);
      htmlBytes = Storage.exists(ARTICLE_TMP_FILE) ? Storage.open(ARTICLE_TMP_FILE).size() : 0;
    }

    LOG_DBG("RSS", "Article fetch %s, html bytes=%zu: %s", fetched ? "ok" : "partial/failed", htmlBytes, url.c_str());
    if (!fetched && htmlBytes == 0) {
      Storage.remove(ARTICLE_TMP_FILE);
      return std::string{};
    }

    const size_t maxAlloc = ESP.getMaxAllocHeap();
    const size_t readBytes = std::min(htmlBytes, maxAlloc > 8192 ? maxAlloc - 8192 : 0);
    if (readBytes < 4096) {
      LOG_ERR("RSS", "Not enough heap to load article HTML: bytes=%zu max=%zu", htmlBytes, maxAlloc);
      Storage.remove(ARTICLE_TMP_FILE);
      return std::string{};
    }

    std::string html;
    html.resize(readBytes);
    HalFile file = Storage.open(ARTICLE_TMP_FILE);
    const int bytesRead = file.read(html.data(), readBytes);
    file.close();
    Storage.remove(ARTICLE_TMP_FILE);
    if (bytesRead <= 0) return std::string{};
    html.resize(static_cast<size_t>(bytesRead));

    std::string extracted = HtmlArticleExtractor::extractReadableText(html);
    LOG_DBG("RSS", "Article extracted bytes=%zu", extracted.size());
    return extracted;
  };

  std::string extracted = fetchAndExtract(item.link);
  const std::string wikiUrl = wikipediaRenderUrl(item.link);
  if (extracted.empty() && !wikiUrl.empty()) {
    LOG_DBG("RSS", "Retrying Wikipedia render endpoint: %s", wikiUrl.c_str());
    if (fontCache) fontCache->clearCache();
    extracted = fetchAndExtract(wikiUrl);
  }
  return extracted;
}

void RssFeedBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed();
    return;
  }
  launchWifiSelection();
}

void RssFeedBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssFeedBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
