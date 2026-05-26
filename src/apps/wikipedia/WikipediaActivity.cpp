#include "WikipediaActivity.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/WifiConnectHelper.h"
#include "activities/util/DownloadWatchdog.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {
std::string sanitizeFilename(const std::string &title) {
  std::string filename = "";
  for (char c : title) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' ||
        c == '_') {
      filename += c;
    }
  }
  return filename;
}

std::string urlEncode(const std::string &value) {
  std::string escaped = "";
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      escaped += c;
    } else if (c == ' ') {
      escaped += "%20";
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", static_cast<unsigned char>(c));
      escaped += hex;
    }
  }
  return escaped;
}
} // namespace

void WikipediaActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/wikipedia");

  errorMessage.clear();
  loadOfflineArticlesList();
  state = WikiState::OfflineList;
  selectedIndex = 0;
  listScrollOffset = 0;
  wifiConnecting = false;
  requestUpdate();
}

void WikipediaActivity::onExit() { Activity::onExit(); }

void WikipediaActivity::loadOfflineArticlesList() {
  offlineArticles.clear();
  std::vector<String> files = Storage.listFiles("/apps/wikipedia");
  for (const auto &file : files) {
    std::string filename = file.c_str();
    if (filename.length() > 4 &&
        filename.substr(filename.length() - 4) == ".txt") {
      offlineArticles.push_back(filename.substr(0, filename.length() - 4));
    }
  }
  std::sort(offlineArticles.begin(), offlineArticles.end());
}

void WikipediaActivity::loadOfflineArticle(const std::string &title) {
  std::string filepath = "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
  String text = Storage.readFile(filepath.c_str());
  currentArticleTitle = title;
  currentArticleText = text.c_str();
  errorMessage.clear();
}

void WikipediaActivity::saveArticleText(const std::string &title,
                                        const std::string &text) {
  std::string filepath = "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
  Storage.writeFile(filepath.c_str(), String(text.c_str()));
}

void WikipediaActivity::ensureWifiConnected() {
  // Do nothing
}

void WikipediaActivity::doSearch() {
  ensureWifiConnected();
  if (WiFi.status() != WL_CONNECTED) {
    errorMessage = "Could not connect to WiFi.";
    searchResults.clear();
    return;
  }

  std::string url =
      "https://en.wikipedia.org/w/api.php?action=opensearch&search=" +
      urlEncode(searchQuery) + "&limit=10&namespace=0&format=json";
  const char *tempPath = "/apps/wikipedia/search.tmp";
  
  DownloadWatchdog::start(15000);
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr);
  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("WIKI", "Search timed out! Crashing and returning home.");
    activityManager.goHome();
    return;
  }

  if (result != HttpDownloader::OK) {
    errorMessage = "Search request failed.";
    searchResults.clear();
    Storage.remove(tempPath);
    return;
  }

  HalFile file;
  if (Storage.openFileForRead("WIKI", tempPath, file)) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    Storage.remove(tempPath);

    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      if (arr.size() >= 2) {
        JsonArray titles = arr[1].as<JsonArray>();
        searchResults.clear();
        for (JsonVariant val : titles) {
          searchResults.push_back(val.as<std::string>());
        }
        errorMessage.clear();
      } else {
        errorMessage = "Invalid response format.";
      }
    } else {
      errorMessage = "Failed to parse JSON.";
    }
  } else {
    errorMessage = "Failed to open temporary file.";
    Storage.remove(tempPath);
  }
}

void WikipediaActivity::doFetchArticle() {
  ensureWifiConnected();
  if (WiFi.status() != WL_CONNECTED) {
    errorMessage = "Could not connect to WiFi.";
    return;
  }

  std::string url = "https://en.wikipedia.org/w/"
                    "api.php?action=query&prop=extracts&explaintext=&titles=" +
                    urlEncode(articleToFetch) + "&format=json&redirects=1";
  const char *tempPath = "/apps/wikipedia/article.tmp";
  
  DownloadWatchdog::start(15000);
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr);
  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("WIKI", "Article fetch timed out! Crashing and returning home.");
    activityManager.goHome();
    return;
  }

  if (result != HttpDownloader::OK) {
    errorMessage = "Failed to download article.";
    Storage.remove(tempPath);
    return;
  }

  HalFile file;
  if (Storage.openFileForRead("WIKI", tempPath, file)) {
    JsonDocument filter;
    filter["query"]["pages"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, file, DeserializationOption::Filter(filter));
    file.close();
    Storage.remove(tempPath);

    if (!err) {
      JsonObject pages = doc["query"]["pages"].as<JsonObject>();
      bool found = false;
      for (JsonPair p : pages) {
        std::string title = p.value()["title"] | "";
        std::string extract = p.value()["extract"] | "";
        if (!title.empty()) {
          currentArticleTitle = title;
          currentArticleText = extract;
          saveArticleText(title, extract);
          errorMessage.clear();
          found = true;
          break;
        }
      }
      if (!found) {
        errorMessage = "Article not found.";
      }
    } else {
      errorMessage = "Failed to parse article JSON.";
    }
  } else {
    errorMessage = "Failed to open temp file.";
    Storage.remove(tempPath);
  }
}

void WikipediaActivity::loop() {

  if (pendingSearch) {
    doSearch();
    pendingSearch = false;
    selectedIndex = 0;
    listScrollOffset = 0;
    state = WikiState::SearchResults;
    requestUpdate();
    return;
  }

  if (pendingArticle) {
    doFetchArticle();
    pendingArticle = false;
    articleScrollOffset = 0;
    state = WikiState::ArticleView;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == WikiState::ArticleView) {
      if (searchResults.empty()) {
        state = WikiState::OfflineList;
        loadOfflineArticlesList();
      } else {
        state = WikiState::SearchResults;
      }
      requestUpdate();
    } else if (state == WikiState::SearchResults) {
      state = WikiState::OfflineList;
      loadOfflineArticlesList();
      selectedIndex = 0;
      listScrollOffset = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == WikiState::OfflineList) {
    int totalItems = static_cast<int>(offlineArticles.size()) + 2;
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedIndex == 0) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(
            renderer, mappedInput, "Wikipedia Search", "", 40);
        startActivityForResult(
            std::move(keyboard), [this](const ActivityResult &result) {
              if (!result.isCancelled) {
                auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
                if (keyboardResult && !keyboardResult->text.empty()) {
                  searchQuery = keyboardResult->text;
                  state = WikiState::Loading;
                  pendingSearch = true;
                }
              }
              requestUpdate();
            });
      } else if (selectedIndex == 1) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
      } else {
        std::string title = offlineArticles[selectedIndex - 2];
        loadOfflineArticle(title);
        state = WikiState::ArticleView;
        requestUpdate();
      }
    }
    return;
  }

  if (state == WikiState::SearchResults) {
    if (!errorMessage.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        state = WikiState::Loading;
        pendingSearch = true;
        errorMessage.clear();
        requestUpdate();
        return;
      }
    } else if (searchResults.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(
            renderer, mappedInput, "Wikipedia Search", "", 40);
        startActivityForResult(
            std::move(keyboard), [this](const ActivityResult &result) {
              if (!result.isCancelled) {
                auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
                if (keyboardResult && !keyboardResult->text.empty()) {
                  searchQuery = keyboardResult->text;
                  state = WikiState::Loading;
                  pendingSearch = true;
                }
              }
              requestUpdate();
            });
      }
    } else {
      int totalItems = static_cast<int>(searchResults.size());
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        selectedIndex = (selectedIndex + 1) % totalItems;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        std::string title = searchResults[selectedIndex];
        std::string filepath =
            "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
        if (Storage.exists(filepath.c_str())) {
          loadOfflineArticle(title);
          state = WikiState::ArticleView;
          requestUpdate();
        } else {
          articleToFetch = title;
          state = WikiState::Loading;
          pendingArticle = true;
          requestUpdate();
        }
      }
    }
    return;
  }

  if (state == WikiState::ArticleView) {
    const auto pageHeight = renderer.getScreenHeight();
    const auto &metrics = UITheme::getInstance().getMetrics();
    const int contentTop =
        metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom =
        pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int cardH = contentBottom - contentTop;
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int linesPerPage = (cardH - 60) / lineHeight;

    int maxOffset =
        std::max(0, static_cast<int>(articleLines.size()) - linesPerPage);

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      articleScrollOffset = std::max(0, articleScrollOffset - 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      articleScrollOffset = std::min(maxOffset, articleScrollOffset + 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      articleScrollOffset = std::max(0, articleScrollOffset - linesPerPage);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      articleScrollOffset =
          std::min(maxOffset, articleScrollOffset + linesPerPage);
      requestUpdate();
    }
    return;
  }
}

void WikipediaActivity::render(RenderLock &&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto &metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Wikipedia");

  const int contentTop =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = contentBottom - contentTop;

  if (state == WikiState::Loading) {
    int textY = contentTop + contentHeight / 2 - 20;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading Wikipedia...");
    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == WikiState::OfflineList) {
    GUI.drawButtonMenu(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        offlineArticles.size() + 2, selectedIndex,
        [this](int index) {
          if (index == 0)
            return std::string("[+ Search Wikipedia]");
          if (index == 1)
            return std::string("[* WiFi Settings]");
          return offlineArticles[index - 2];
        },
        [this](int index) {
          if (index == 0)
            return UIIcon::File;
          if (index == 1)
            return UIIcon::Library;
          return UIIcon::Book;
        });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == WikiState::SearchResults) {
    if (!errorMessage.empty()) {
      int textY = contentTop + contentHeight / 2 - 40;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, errorMessage.c_str(),
                                true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(SMALL_FONT_ID, textY + 30,
                                "Configure your network or retry search.");
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), "WiFi", nullptr, "Retry");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else if (searchResults.empty()) {
      int textY = contentTop + contentHeight / 2 - 20;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "No results found.");
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), "Search", nullptr, nullptr);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      GUI.drawButtonMenu(
          renderer, Rect{0, contentTop, pageWidth, contentHeight},
          searchResults.size(), selectedIndex,
          [this](int index) { return searchResults[index]; },
          [this](int index) {
            std::string title = searchResults[index];
            std::string filepath =
                "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
            if (Storage.exists(filepath.c_str())) {
              return UIIcon::Book; // Cached offline
            }
            return UIIcon::Wifi; // Remote online
          });

      const auto labels = mappedInput.mapLabels(
          tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == WikiState::ArticleView) {
    const int cardX = metrics.contentSidePadding;
    const int cardY = contentTop;
    const int cardW = pageWidth - 2 * metrics.contentSidePadding;
    const int cardH = contentHeight;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 10, true);

    // Header Title Card
    renderer.drawText(SMALL_FONT_ID, cardX + 20, cardY + 20,
                      currentArticleTitle.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawLine(cardX + 20, cardY + 40, cardX + cardW - 20, cardY + 40, 1,
                      true);

    // Compute lines
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int linesPerPage = (cardH - 60) / lineHeight;

    articleLines =
        renderer.wrappedText(SMALL_FONT_ID, currentArticleText.c_str(),
                             cardW - 40, 2000, EpdFontFamily::REGULAR);

    for (int i = 0; i < linesPerPage; i++) {
      int idx = articleScrollOffset + i;
      if (idx >= static_cast<int>(articleLines.size()))
        break;
      renderer.drawText(SMALL_FONT_ID, cardX + 20, cardY + 50 + i * lineHeight,
                        articleLines[idx].c_str(), true,
                        EpdFontFamily::REGULAR);
    }

    // Paging Indicator
    if (!articleLines.empty()) {
      int currentLineEnd = std::min(static_cast<int>(articleLines.size()),
                                    articleScrollOffset + linesPerPage);
      std::string pageStr = std::to_string(currentLineEnd) + " / " +
                            std::to_string(articleLines.size()) + " lines";
      int pageW = renderer.getTextWidth(SMALL_FONT_ID, pageStr.c_str());
      renderer.drawText(SMALL_FONT_ID, cardX + cardW - 20 - pageW, cardY + 20,
                        pageStr.c_str(), true, EpdFontFamily::REGULAR);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr,
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  }

  renderer.displayBuffer();
}
