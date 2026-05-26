#include "RssActivity.h"
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
std::string cleanText(const std::string &input) {
  std::string output = input;
  size_t pos;
  while ((pos = output.find("\n")) != std::string::npos) {
    output.replace(pos, 1, " ");
  }
  while ((pos = output.find("\r")) != std::string::npos) {
    output.replace(pos, 1, " ");
  }
  // Trim leading
  output.erase(output.begin(),
               std::find_if(output.begin(), output.end(), [](unsigned char ch) {
                 return !std::isspace(ch);
               }));
  // Trim trailing
  output.erase(std::find_if(output.rbegin(), output.rend(),
                            [](unsigned char ch) { return !std::isspace(ch); })
                   .base(),
               output.end());

  return output;
}

std::string getDomainName(const std::string &url) {
  size_t start = url.find("://");
  if (start == std::string::npos)
    return url;
  start += 3;
  size_t end = url.find("/", start);
  if (end == std::string::npos)
    end = url.length();
  std::string domain = url.substr(start, end - start);
  if (domain.rfind("www.", 0) == 0) {
    domain = domain.substr(4);
  }
  return domain;
}

void parseRSSFeed(const std::string &xml, const std::string &feedUrl,
                  std::vector<RssItem> &items) {
  std::string feedName = getDomainName(feedUrl);
  // Look for channel title
  size_t chanTitleStart = xml.find("<title>");
  if (chanTitleStart != std::string::npos) {
    size_t chanTitleEnd = xml.find("</title>", chanTitleStart);
    if (chanTitleEnd != std::string::npos) {
      std::string potentialName =
          xml.substr(chanTitleStart + 7, chanTitleEnd - (chanTitleStart + 7));
      size_t firstItem = xml.find("<item>");
      if (firstItem == std::string::npos)
        firstItem = xml.find("<entry>");
      if (firstItem == std::string::npos || chanTitleStart < firstItem) {
        if (potentialName.find("<![CDATA[") != std::string::npos) {
          potentialName.erase(potentialName.find("<![CDATA["), 9);
          size_t cdataEnd = potentialName.find("]]>");
          if (cdataEnd != std::string::npos)
            potentialName.erase(cdataEnd, 3);
        }
        potentialName = cleanText(potentialName);
        if (!potentialName.empty() && potentialName.length() < 30) {
          feedName = potentialName;
        }
      }
    }
  }

  size_t pos = 0;
  int count = 0;
  while (count < 15) { // Limit to 15 items per feed
    size_t itemStart = xml.find("<item>", pos);
    size_t itemEnd = std::string::npos;
    bool isEntry = false;
    if (itemStart == std::string::npos) {
      itemStart = xml.find("<entry>", pos);
      if (itemStart != std::string::npos) {
        isEntry = true;
        itemEnd = xml.find("</entry>", itemStart);
      }
    } else {
      itemEnd = xml.find("</item>", itemStart);
    }

    if (itemStart == std::string::npos || itemEnd == std::string::npos) {
      break;
    }

    std::string itemXml = xml.substr(itemStart, itemEnd - itemStart);
    pos = itemEnd + 7;

    std::string title;
    size_t titleStart = itemXml.find("<title>");
    if (titleStart != std::string::npos) {
      size_t titleEnd = itemXml.find("</title>", titleStart);
      if (titleEnd != std::string::npos) {
        title = itemXml.substr(titleStart + 7, titleEnd - (titleStart + 7));
      }
    }

    std::string link;
    size_t linkStart = itemXml.find("<link>");
    if (linkStart != std::string::npos) {
      size_t linkEnd = itemXml.find("</link>", linkStart);
      if (linkEnd != std::string::npos) {
        link = itemXml.substr(linkStart + 6, linkEnd - (linkStart + 6));
      }
    } else {
      size_t atomLinkStart = itemXml.find("<link");
      if (atomLinkStart != std::string::npos) {
        size_t hrefStart = itemXml.find("href=\"", atomLinkStart);
        if (hrefStart != std::string::npos) {
          size_t hrefEnd = itemXml.find("\"", hrefStart + 6);
          if (hrefEnd != std::string::npos) {
            link = itemXml.substr(hrefStart + 6, hrefEnd - (hrefStart + 6));
          }
        }
      }
    }

    auto cleanCDATA = [](std::string &s) {
      size_t cdata = s.find("<![CDATA[");
      if (cdata != std::string::npos) {
        s.erase(cdata, 9);
        size_t cdataEnd = s.find("]]>");
        if (cdataEnd != std::string::npos) {
          s.erase(cdataEnd, 3);
        }
      }
    };
    cleanCDATA(title);
    cleanCDATA(link);

    auto unescape = [](std::string &s) {
      size_t p;
      while ((p = s.find("&amp;")) != std::string::npos)
        s.replace(p, 5, "&");
      while ((p = s.find("&lt;")) != std::string::npos)
        s.replace(p, 4, "<");
      while ((p = s.find("&gt;")) != std::string::npos)
        s.replace(p, 4, ">");
      while ((p = s.find("&quot;")) != std::string::npos)
        s.replace(p, 6, "\"");
      while ((p = s.find("&apos;")) != std::string::npos)
        s.replace(p, 6, "'");
      while ((p = s.find("&#39;")) != std::string::npos)
        s.replace(p, 5, "'");
      while ((p = s.find("&#039;")) != std::string::npos)
        s.replace(p, 6, "'");
    };

    title = cleanText(title);
    link = cleanText(link);
    unescape(title);
    unescape(link);

    if (!title.empty()) {
      RssItem item;
      item.title = title;
      item.link = link;
      item.feedName = feedName;
      items.push_back(item);
      count++;
    }
  }
}
} // namespace

namespace {
static void rssFetchTaskFunc(void* param) {
  RssActivity* activity = static_cast<RssActivity*>(param);
  activity->runBackgroundFetch();
  vTaskDelete(nullptr);
}
} // namespace

void RssActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/rss");

  loadSubscriptions();
  errorMessage.clear();
  wifiConnecting = false;
  backgroundFetchFailed = false;
  pendingUpdateItems = false;
  fetchTaskHandle = nullptr;

  if (loadCache()) {
    state = RssState::FeedList;
    selectedItemIndex = 0;
    itemsScrollOffset = 0;
    isRefreshing = false;
    pendingFetch = false;
  } else {
    state = RssState::Loading;
    isRefreshing = true;
    pendingFetch = true;
  }
  requestUpdate();
}

void RssActivity::onExit() {
  Activity::onExit();
  DownloadWatchdog::stop();
  if (fetchTaskHandle != nullptr) {
    WiFi.disconnect();
    int waitCount = 0;
    while (fetchTaskHandle != nullptr && waitCount < 100) {
      delay(10);
      waitCount++;
    }
    if (fetchTaskHandle != nullptr) {
      TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
      fetchTaskHandle = nullptr;
      vTaskDelete(tempHandle);
    }
  }
}

void RssActivity::loadSubscriptions() {
  subscriptions.clear();
  String input = Storage.readFile("/apps/rss/subscriptions.txt");
  if (input.length() == 0) {
    subscriptions.push_back("https://www.huffpost.com/section/world-news/feed");
    subscriptions.push_back(
        "https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml");
    subscriptions.push_back("http://feeds.washingtonpost.com/rss/world");
    subscriptions.push_back("http://rss.cnn.com/rss/edition.rss");
    subscriptions.push_back("https://news.yahoo.com/rss/mostviewed");
    saveSubscriptions();
    return;
  }

  int start = 0;
  while (start < input.length()) {
    int end = input.indexOf('\n', start);
    if (end == -1) {
      end = input.length();
    }
    String line = input.substring(start, end);
    line.trim();
    if (line.length() > 0) {
      subscriptions.push_back(line.c_str());
    }
    start = end + 1;
  }
}

void RssActivity::saveSubscriptions() {
  String output = "";
  for (const auto &sub : subscriptions) {
    output += String(sub.c_str()) + "\n";
  }
  Storage.writeFile("/apps/rss/subscriptions.txt", output);
}

void RssActivity::saveCache() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &item : items) {
    JsonObject obj = arr.add<JsonObject>();
    obj["t"] = item.title;
    obj["l"] = item.link;
    obj["f"] = item.feedName;
  }
  String output;
  serializeJson(doc, output);
  Storage.writeFile("/apps/rss/cache.json", output);
}

bool RssActivity::loadCache() {
  String input = Storage.readFile("/apps/rss/cache.json");
  if (input.length() == 0)
    return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err)
    return false;

  items.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    RssItem item;
    item.title = obj["t"] | "";
    item.link = obj["l"] | "";
    item.feedName = obj["f"] | "";
    items.push_back(item);
  }
  return !items.empty();
}

void RssActivity::saveMarkdown() {
  String md = "# RSS News Feed\n\n";
  md += "*Last updated: RSS App*\n\n";
  md += "---\n\n";
  for (const auto &item : items) {
    md += "## " + String(item.title.c_str()) + "\n";
    md += "- **Source**: " + String(item.feedName.c_str()) + "\n";
    md += "- **Link**: [" + String(item.link.c_str()) + "](" +
          String(item.link.c_str()) + ")\n\n";
    md += "---\n\n";
  }
  Storage.writeFile("/rss_feed.md", md);
}

void RssActivity::ensureWifiConnected() {
  // Do nothing
}

bool RssActivity::fetchFeeds() {
  std::vector<std::vector<RssItem>> feedLists;
  ensureWifiConnected();
  if (WiFi.status() != WL_CONNECTED) {
    errorMessage = "Could not connect to WiFi.";
    return false;
  }

  const char *tempXmlPath = "/apps/rss/temp.xml";
  for (const auto &url : subscriptions) {
    if (fetchTaskHandle == nullptr) {
      // If task handle was cleared/cancelled, abort
      break;
    }
    auto result =
        HttpDownloader::downloadToFile(url.c_str(), tempXmlPath, nullptr);
    if (result == HttpDownloader::OK) {
      HalFile file;
      if (Storage.openFileForRead("RSS", tempXmlPath, file)) {
        size_t fileSize = file.size();
        std::string xmlContent;
        xmlContent.resize(fileSize);
        file.read(&xmlContent[0], fileSize);
        file.close();

        std::vector<RssItem> feedItems;
        parseRSSFeed(xmlContent, url, feedItems);
        if (!feedItems.empty()) {
          feedLists.push_back(feedItems);
        }
      }
    }
    Storage.remove(tempXmlPath);
    delay(100);
  }

  if (feedLists.empty()) {
    errorMessage = "Failed to download feeds.";
    return false;
  }

  fetchedItems.clear();
  size_t maxItems = 0;
  for (const auto &list : feedLists) {
    if (list.size() > maxItems)
      maxItems = list.size();
  }

  for (size_t i = 0; i < maxItems; i++) {
    for (const auto &list : feedLists) {
      if (i < list.size()) {
        fetchedItems.push_back(list[i]);
      }
    }
  }

  errorMessage.clear();
  return true;
}

void RssActivity::performFetch() {
  if (fetchTaskHandle != nullptr) return;
  isRefreshing = true;
  backgroundFetchFailed = false;
  pendingUpdateItems = false;
  
  xTaskCreate(rssFetchTaskFunc, "rss_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void RssActivity::runBackgroundFetch() {
  DownloadWatchdog::start(25000);
  bool success = fetchFeeds();
  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("RSS", "Background refresh timed out!");
    backgroundFetchFailed = true;
    errorMessage = "Refresh timed out.";
  } else if (!success) {
    backgroundFetchFailed = true;
  } else {
    backgroundFetchFailed = false;
    errorMessage.clear();
  }

  pendingUpdateItems = true;
}

void RssActivity::loop() {

  if (pendingUpdateItems) {
    pendingUpdateItems = false;
    isRefreshing = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("RSS", "Watchdog timeout! Crashing to home screen.");
      activityManager.goHome();
      return;
    }
    if (!backgroundFetchFailed) {
      items = std::move(fetchedItems);
      saveCache();
      saveMarkdown();
      selectedItemIndex = 0;
      itemsScrollOffset = 0;
      if (state == RssState::Loading) {
        state = RssState::FeedList;
      }
    } else {
      if (items.empty()) {
        state = RssState::Loading;
      }
    }
    requestUpdate();
  }

  if (pendingFetch) {
    performFetch();
    pendingFetch = false;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == RssState::ItemDetail) {
      state = RssState::FeedList;
      requestUpdate();
    } else if (state == RssState::ManageSubscriptions) {
      state = RssState::FeedList;
      selectedItemIndex = 0;
      itemsScrollOffset = 0;
      isRefreshing = true;
      pendingFetch = true;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == RssState::Loading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activityManager.pushActivity(
          std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      state = RssState::ManageSubscriptions;
      selectedSubIndex = 0;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      isRefreshing = true;
      pendingFetch = true;
      requestUpdate();
      return;
    }
    return;
  }

  if (state == RssState::FeedList) {
    if (items.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = RssState::ManageSubscriptions;
        selectedSubIndex = 0;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        isRefreshing = true;
        pendingFetch = true;
        requestUpdate();
        return;
      }
    } else {
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (selectedItemIndex == -1) {
          selectedItemIndex = items.size() - 1;
        } else if (selectedItemIndex == 0) {
          selectedItemIndex = -1;
          selectedHeaderAction = 0;
        } else {
          selectedItemIndex--;
        }

        // Scroll adjustment
        if (selectedItemIndex >= 0) {
          if (selectedItemIndex < itemsScrollOffset) {
            itemsScrollOffset = selectedItemIndex;
          } else if (selectedItemIndex >= itemsScrollOffset + 5) {
            itemsScrollOffset = selectedItemIndex - 4;
          }
        }
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (selectedItemIndex == -1) {
          selectedItemIndex = 0;
        } else if (selectedItemIndex == static_cast<int>(items.size()) - 1) {
          selectedItemIndex = -1;
          selectedHeaderAction = 0;
        } else {
          selectedItemIndex++;
        }

        // Scroll adjustment
        if (selectedItemIndex >= 0) {
          if (selectedItemIndex < itemsScrollOffset) {
            itemsScrollOffset = selectedItemIndex;
          } else if (selectedItemIndex >= itemsScrollOffset + 5) {
            itemsScrollOffset = selectedItemIndex - 4;
          }
        }
        requestUpdate();
      } else if (selectedItemIndex == -1) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
          if (selectedHeaderAction > 0) {
            selectedHeaderAction--;
            requestUpdate();
          }
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
          if (selectedHeaderAction < 2) {
            selectedHeaderAction++;
            requestUpdate();
          }
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          if (selectedHeaderAction == 0) {
            isRefreshing = true;
            pendingFetch = true;
            selectedItemIndex = 0;
            requestUpdate();
          } else if (selectedHeaderAction == 1) {
            state = RssState::ManageSubscriptions;
            selectedSubIndex = 0;
            requestUpdate();
          } else if (selectedHeaderAction == 2) {
            activityManager.pushActivity(
                std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
          }
        }
      } else {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          state = RssState::ItemDetail;
          requestUpdate();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
          state = RssState::ManageSubscriptions;
          selectedSubIndex = 0;
          requestUpdate();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
          isRefreshing = true;
          pendingFetch = true;
          selectedItemIndex = 0;
          requestUpdate();
        }
      }
    }
    return;
  }

  if (state == RssState::ManageSubscriptions) {
    int totalItems = static_cast<int>(subscriptions.size()) + 1;
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedSubIndex = (selectedSubIndex - 1 + totalItems) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedSubIndex = (selectedSubIndex + 1) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedSubIndex == 0) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(
            renderer, mappedInput, "Add RSS Feed URL", "", 128);
        startActivityForResult(
            std::move(keyboard), [this](const ActivityResult &result) {
              if (!result.isCancelled) {
                auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
                if (keyboardResult && !keyboardResult->text.empty()) {
                  std::string url = keyboardResult->text;
                  if (std::find(subscriptions.begin(), subscriptions.end(),
                                url) == subscriptions.end()) {
                    subscriptions.push_back(url);
                    saveSubscriptions();
                  }
                }
              }
              requestUpdate();
            });
      } else {
        std::string urlToDelete = subscriptions[selectedSubIndex - 1];
        auto handler = [this, urlToDelete](const ActivityResult &res) {
          if (!res.isCancelled) {
            auto it = std::find(subscriptions.begin(), subscriptions.end(),
                                urlToDelete);
            if (it != subscriptions.end()) {
              subscriptions.erase(it);
              saveSubscriptions();
              selectedSubIndex = 0;
            }
          }
          requestUpdate();
        };
        startActivityForResult(std::make_unique<ConfirmationActivity>(
                                   renderer, mappedInput, "Delete Feed?",
                                   getDomainName(urlToDelete)),
                               handler);
      }
    }
    return;
  }
}

void RssActivity::render(RenderLock &&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto &metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "RSS Feeds");

  if (state == RssState::FeedList && isRefreshing) {
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding,
                      metrics.topPadding + 5, "Refreshing feed...", true,
                      EpdFontFamily::REGULAR);
  }

  const int contentTop =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = contentBottom - contentTop;

  if (state == RssState::Loading) {
    int textY = contentTop + contentHeight / 2 - 40;
    if (!errorMessage.empty()) {
      renderer.drawCenteredText(UI_12_FONT_ID, textY, errorMessage.c_str(),
                                true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(
          SMALL_FONT_ID, textY + 30,
          "Press WiFi Settings to configure net, or Retry.");
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), "WiFi", "Subscriptions", "Retry");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading RSS feeds...");
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == RssState::FeedList) {
    // Top bar buttons: Refresh Feed / Subscriptions / WiFi
    const int statusY = contentTop;
    const int statusH = 32;
    const int statusW = (pageWidth - 2 * metrics.contentSidePadding - 24) / 3;
    const int statusX1 = metrics.contentSidePadding;
    const int statusX2 = statusX1 + statusW + 12;
    const int statusX3 = statusX2 + statusW + 12;

    bool headerSelected = (selectedItemIndex == -1);
    bool refreshSelected = (headerSelected && selectedHeaderAction == 0);
    bool subsSelected = (headerSelected && selectedHeaderAction == 1);
    bool wifiSelected = (headerSelected && selectedHeaderAction == 2);

    // Refresh Feed button
    renderer.drawRoundedRect(statusX1, statusY, statusW, statusH, 1, 6, true);
    if (refreshSelected) {
      renderer.fillRoundedRect(statusX1, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string refText = offlineMode ? "Offline" : "Refresh";
    int refTxtW = renderer.getTextWidth(SMALL_FONT_ID, refText.c_str());
    renderer.drawText(SMALL_FONT_ID, statusX1 + (statusW - refTxtW) / 2,
                      statusY +
                          (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                      refText.c_str(), !refreshSelected);

    // Subscriptions button
    renderer.drawRoundedRect(statusX2, statusY, statusW, statusH, 1, 6, true);
    if (subsSelected) {
      renderer.fillRoundedRect(statusX2, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string subsText = "Subs";
    int subsTxtW = renderer.getTextWidth(SMALL_FONT_ID, subsText.c_str());
    renderer.drawText(SMALL_FONT_ID, statusX2 + (statusW - subsTxtW) / 2,
                      statusY +
                          (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                      subsText.c_str(), !subsSelected);

    // WiFi button
    renderer.drawRoundedRect(statusX3, statusY, statusW, statusH, 1, 6, true);
    if (wifiSelected) {
      renderer.fillRoundedRect(statusX3, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string wifiText = "WiFi";
    int wifiTxtW = renderer.getTextWidth(SMALL_FONT_ID, wifiText.c_str());
    renderer.drawText(SMALL_FONT_ID, statusX3 + (statusW - wifiTxtW) / 2,
                      statusY +
                          (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                      wifiText.c_str(), !wifiSelected);

    if (items.empty()) {
      int textY = statusY + statusH + 60;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "No feeds loaded.");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "WiFi",
                                                "Subscriptions", "Refresh");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      const int listTop = statusY + statusH + 12;
      const int cellH = 82;
      const int spacing = 8;

      for (int i = 0; i < 5; i++) {
        int idx = itemsScrollOffset + i;
        if (idx >= static_cast<int>(items.size()))
          break;

        const auto &item = items[idx];
        int cellY = listTop + i * (cellH + spacing);
        int cellX = metrics.contentSidePadding;
        int cellW = pageWidth - 2 * metrics.contentSidePadding;

        bool isSelected = (idx == selectedItemIndex);
        renderer.drawRoundedRect(cellX, cellY, cellW, cellH, isSelected ? 3 : 1,
                                 8, true);

        // Draw Feed Source label
        renderer.drawText(SMALL_FONT_ID, cellX + 12, cellY + 12,
                          item.feedName.c_str(), true, EpdFontFamily::BOLD);

        // Draw Title (wrapped 2 lines max)
        auto titleLines =
            renderer.wrappedText(SMALL_FONT_ID, item.title.c_str(), cellW - 24,
                                 2, EpdFontFamily::REGULAR);
        for (size_t l = 0; l < titleLines.size(); l++) {
          renderer.drawText(SMALL_FONT_ID, cellX + 12, cellY + 32 + l * 18,
                            titleLines[l].c_str(), true,
                            EpdFontFamily::REGULAR);
        }
      }

      const auto labels = mappedInput.mapLabels(
          tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == RssState::ItemDetail) {
    const auto &item = items[selectedItemIndex];

    const int cardX = metrics.contentSidePadding;
    const int cardY = contentTop;
    const int cardW = pageWidth - 2 * metrics.contentSidePadding;
    const int cardH = contentHeight;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 10, true);

    // Source Label
    renderer.drawText(SMALL_FONT_ID, cardX + 20, cardY + 20,
                      item.feedName.c_str(), true, EpdFontFamily::BOLD);

    // Title (wrapped)
    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, item.title.c_str(),
                                           cardW - 40, 5, EpdFontFamily::BOLD);
    int currY = cardY + 45;
    for (size_t l = 0; l < titleLines.size(); l++) {
      renderer.drawText(UI_12_FONT_ID, cardX + 20, currY, titleLines[l].c_str(),
                        true, EpdFontFamily::BOLD);
      currY += 24;
    }

    currY += 15;
    renderer.drawLine(cardX + 20, currY, cardX + cardW - 20, currY, 1, true);
    currY += 20;

    // Link URL (wrapped)
    renderer.drawText(SMALL_FONT_ID, cardX + 20, currY, "Link URL:", true,
                      EpdFontFamily::BOLD);
    currY += 18;
    auto linkLines =
        renderer.wrappedText(SMALL_FONT_ID, item.link.c_str(), cardW - 40, 4,
                             EpdFontFamily::REGULAR);
    for (size_t l = 0; l < linkLines.size(); l++) {
      renderer.drawText(SMALL_FONT_ID, cardX + 20, currY, linkLines[l].c_str(),
                        true, EpdFontFamily::REGULAR);
      currY += 18;
    }

    currY += 30;
    auto noteLines = renderer.wrappedText(
        SMALL_FONT_ID,
        "Note: This feed list is automatically saved to /rss_feed.md on the SD "
        "card root for beautiful offline reading.",
        cardW - 40, 3, EpdFontFamily::ITALIC);
    for (size_t l = 0; l < noteLines.size(); l++) {
      renderer.drawText(SMALL_FONT_ID, cardX + 20, currY, noteLines[l].c_str(),
                        true, EpdFontFamily::ITALIC);
      currY += 18;
    }

    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == RssState::ManageSubscriptions) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Subscriptions");

    GUI.drawButtonMenu(
        renderer,
        Rect{
            0,
            metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
            pageWidth,
            pageHeight - (metrics.headerHeight + metrics.topPadding +
                          metrics.verticalSpacing + metrics.buttonHintsHeight)},
        subscriptions.size() + 1, selectedSubIndex,
        [this](int index) {
          if (index == 0)
            return std::string("[+ Add RSS Feed URL]");
          return getDomainName(subscriptions[index - 1]);
        },
        [this](int index) {
          if (index == 0)
            return UIIcon::File;
          return UIIcon::Library;
        });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  }

  renderer.displayBuffer();
}
