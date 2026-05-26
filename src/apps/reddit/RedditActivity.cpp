#include "RedditActivity.h"
#include <algorithm>

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <cstdlib>

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

namespace {
// Helper to replace HTML entities or strip basic formatting if needed
std::string cleanText(const std::string &input) {
  std::string output = input;
  // Replace simple HTML entities
  size_t pos;
  while ((pos = output.find("&amp;")) != std::string::npos) {
    output.replace(pos, 5, "&");
  }
  while ((pos = output.find("&lt;")) != std::string::npos) {
    output.replace(pos, 4, "<");
  }
  while ((pos = output.find("&gt;")) != std::string::npos) {
    output.replace(pos, 4, ">");
  }
  while ((pos = output.find("\n")) != std::string::npos) {
    output.replace(pos, 1, " ");
  }
  return output;
}
} // namespace

namespace {
static void redditFetchTaskFunc(void* param) {
  RedditActivity* activity = static_cast<RedditActivity*>(param);
  activity->runBackgroundFetch();
  vTaskDelete(nullptr);
}
} // namespace


void RedditActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/reddit");

  loadSubscriptions();
  subscriptionsChanged = false;
  wifiConnecting = false;
  backgroundFetchFailed = false;
  pendingUpdatePosts = false;
  fetchTaskHandle = nullptr;

  if (loadOfflineFeeds()) {
    state = RedditState::PostList;
    selectedPostIndex = 0;
    postsScrollOffset = 0;
    isRefreshing = false;
    pendingFetch = false;
  } else {
    state = RedditState::LoadingPosts;
    isRefreshing = true;
    pendingFetch = true;
  }
  requestUpdate();
}

void RedditActivity::ensureWifiConnected() {
  // Do nothing
}

void RedditActivity::onExit() {
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

void RedditActivity::savePostsCache() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &post : posts) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = post.id;
    obj["title"] = post.title;
    obj["sub"] = post.subreddit;
    obj["auth"] = post.author;
    obj["link"] = post.permalink;
    obj["comments"] = post.numComments;
    obj["ups"] = post.ups;
  }
  String output;
  serializeJson(doc, output);
  Storage.writeFile("/apps/reddit/posts.json", output);
}

bool RedditActivity::loadPostsCache() {
  String input = Storage.readFile("/apps/reddit/posts.json");
  if (input.length() == 0)
    return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err)
    return false;

  posts.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    RedditPost p;
    p.id = obj["id"] | "";
    p.title = obj["title"] | "";
    p.subreddit = obj["sub"] | "";
    p.author = obj["auth"] | "";
    p.permalink = obj["link"] | "";
    p.numComments = obj["comments"] | 0;
    p.ups = obj["ups"] | 0;
    posts.push_back(p);
  }
  return !posts.empty();
}

void RedditActivity::saveCommentsCache(const std::string &id) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &comment : comments) {
    JsonObject obj = arr.add<JsonObject>();
    obj["auth"] = comment.author;
    obj["body"] = comment.body;
  }
  String output;
  serializeJson(doc, output);
  std::string filepath = "/apps/reddit/comments_" + id + ".json";
  Storage.writeFile(filepath.c_str(), output);
}

bool RedditActivity::loadCommentsCache(const std::string &id) {
  std::string filepath = "/apps/reddit/comments_" + id + ".json";
  String input = Storage.readFile(filepath.c_str());
  if (input.length() == 0)
    return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err)
    return false;

  comments.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    RedditComment c;
    c.author = obj["auth"] | "";
    c.body = obj["body"] | "";
    comments.push_back(c);
  }
  return true;
}

void RedditActivity::loadSubscriptions() {
  subscriptions.clear();
  String input = Storage.readFile("/apps/reddit/subscriptions.txt");
  if (input.length() == 0) {
    input = Storage.readFile("/apps/reddit/subreddits.txt");
  }
  if (input.length() == 0) {
    input = Storage.readFile("/apps/reddit/subscriptions.json");
    if (input.length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, input);
      if (!err) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonVariant val : arr) {
          subscriptions.push_back(val.as<std::string>());
        }
        saveSubscriptions();
        Storage.remove("/apps/reddit/subscriptions.json");
        return;
      }
    }
  }

  if (input.length() == 0)
    return;

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

void RedditActivity::saveSubscriptions() {
  String output = "";
  for (const auto &sub : subscriptions) {
    output += String(sub.c_str()) + "\n";
  }
  Storage.writeFile("/apps/reddit/subscriptions.txt", output);
}

std::string RedditActivity::sanitizeSubreddit(const std::string &input) {
  std::string sub = input;
  sub.erase(0, sub.find_first_not_of(" \t\r\n"));
  sub.erase(sub.find_last_not_of(" \t\r\n") + 1);
  if (sub.rfind("r/", 0) == 0) {
    sub = sub.substr(2);
  }
  return sub;
}

void RedditActivity::saveHomeMarkdown() {
  String md;
  md += "# Reddit Subscribed/Home Feed\n\n";
  md += "Generated on device. Total posts: " + String(posts.size()) + "\n\n";
  md += "---\n\n";

  for (size_t i = 0; i < posts.size(); i++) {
    const auto &post = posts[i];
    md += "## " + String(i + 1) + ". r/" + String(post.subreddit.c_str()) +
          " - " + String(post.title.c_str()) + "\n";
    md += "Author: u/" + String(post.author.c_str()) +
          " | Comments count: " + String(post.numComments) + "\n";
    md += "Link: https://www.reddit.com" + String(post.permalink.c_str()) +
          "\n\n";

    std::vector<RedditComment> tempComments;
    std::string filepath = "/apps/reddit/comments_" + post.id + ".json";
    String input = Storage.readFile(filepath.c_str());
    if (input.length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, input);
      if (!err) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
          RedditComment c;
          c.author = obj["auth"] | "";
          c.body = obj["body"] | "";
          tempComments.push_back(c);
        }
      }
    }

    if (!tempComments.empty()) {
      md += "### Top Comments:\n\n";
      for (const auto &comment : tempComments) {
        md += "* **u/" + String(comment.author.c_str()) + "**:\n";
        md += "  " + String(comment.body.c_str()) + "\n\n";
      }
    } else {
      md += "*No comments cached.* (Open post in app while online to cache "
            "comments)\n\n";
    }
    md += "---\n\n";
  }

  Storage.writeFile("/apps/reddit/reddit_home.md", md);
  LOG_DBG("REDDIT",
          "Exported human readable feed to /apps/reddit/reddit_home.md");
}

bool RedditActivity::parsePostsFromFile(const std::string &filepath,
                                        std::vector<RedditPost> &targetList) {
  HalFile file;
  if (!Storage.openFileForRead("REDDIT", filepath, file)) {
    return false;
  }

  JsonDocument filter;
  filter["data"]["children"][0]["data"]["id"] = true;
  filter["data"]["children"][0]["data"]["title"] = true;
  filter["data"]["children"][0]["data"]["subreddit"] = true;
  filter["data"]["children"][0]["data"]["author"] = true;
  filter["data"]["children"][0]["data"]["permalink"] = true;
  filter["data"]["children"][0]["data"]["num_comments"] = true;
  filter["data"]["children"][0]["data"]["ups"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();

  if (err)
    return false;

  if (doc["data"]["children"].isNull())
    return false;
  auto children = doc["data"]["children"].as<JsonArray>();
  for (JsonObject child : children) {
    auto data = child["data"];
    RedditPost p;
    p.id = data["id"] | "";
    p.title = cleanText(data["title"] | "");
    p.subreddit = data["subreddit"] | "";
    p.author = data["author"] | "";
    p.permalink = data["permalink"] | "";
    p.numComments = data["num_comments"] | 0;
    p.ups = data["ups"] | 0;
    targetList.push_back(p);
  }
  return true;
}

bool RedditActivity::loadOfflineFeeds() {
  posts.clear();
  if (subscriptions.empty()) {
    return parsePostsFromFile("/apps/reddit/home.json", posts);
  }

  std::vector<std::vector<RedditPost>> subPostsLists;
  for (const auto &sub : subscriptions) {
    std::vector<RedditPost> subPosts;
    std::string filepath = "/apps/reddit/" + sub + ".json";
    if (parsePostsFromFile(filepath, subPosts)) {
      if (!subPosts.empty()) {
        std::sort(subPosts.begin(), subPosts.end(),
                  [](const RedditPost &a, const RedditPost &b) {
                    return a.ups > b.ups;
                  });
        subPostsLists.push_back(subPosts);
      }
    }
  }

  if (subPostsLists.empty()) {
    return loadPostsCache();
  }

  size_t maxSize = 0;
  for (const auto &list : subPostsLists) {
    if (list.size() > maxSize) {
      maxSize = list.size();
    }
  }

  for (size_t i = 0; i < maxSize; i++) {
    for (const auto &list : subPostsLists) {
      if (i < list.size()) {
        posts.push_back(list[i]);
      }
    }
  }

  return !posts.empty();
}

bool RedditActivity::fetchPostsFromUrl(const std::string &url,
                                       std::vector<RedditPost> &targetList,
                                       const std::string &cachePath) {
  const char *tempPath = "/apps/reddit/posts.tmp";
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr);
  if (result != HttpDownloader::OK) {
    RenderLock lock;
    Storage.remove(tempPath);
    return false;
  }

  std::string finalPath = tempPath;
  if (!cachePath.empty()) {
    RenderLock lock;
    Storage.remove(cachePath.c_str());
    Storage.rename(tempPath, cachePath.c_str());
    finalPath = cachePath;
  }

  bool success = false;
  {
    RenderLock lock;
    success = parsePostsFromFile(finalPath, targetList);
  }

  if (cachePath.empty()) {
    RenderLock lock;
    Storage.remove(tempPath);
  }
  return success;
}

bool RedditActivity::fetchPosts() {
  fetchedPosts.clear();
  if (subscriptions.empty()) {
    return fetchPostsFromUrl("https://www.reddit.com/.json?limit=10", fetchedPosts,
                             "/apps/reddit/home.json");
  }

  std::vector<std::vector<RedditPost>> subPostsLists;
  for (const auto &sub : subscriptions) {
    if (fetchTaskHandle == nullptr) {
      break;
    }
    std::vector<RedditPost> subPosts;
    std::string url = "https://www.reddit.com/r/" + sub + "/.json?limit=10";
    std::string cachePath = "/apps/reddit/" + sub + ".json";

    ensureWifiConnected();
    if (WiFi.status() != WL_CONNECTED)
      continue;

    if (fetchPostsFromUrl(url, subPosts, cachePath)) {
      if (!subPosts.empty()) {
        std::sort(subPosts.begin(), subPosts.end(),
                  [](const RedditPost &a, const RedditPost &b) {
                    return a.ups > b.ups;
                  });
        subPostsLists.push_back(subPosts);
      }
    }
    delay(100);
  }

  if (subPostsLists.empty()) {
    return false;
  }

  size_t maxSize = 0;
  for (const auto &list : subPostsLists) {
    if (list.size() > maxSize) {
      maxSize = list.size();
    }
  }

  for (size_t i = 0; i < maxSize; i++) {
    for (const auto &list : subPostsLists) {
      if (i < list.size()) {
        fetchedPosts.push_back(list[i]);
      }
    }
  }

  return !fetchedPosts.empty();
}

bool RedditActivity::fetchComments(const std::string &permalink) {
  std::string cleanPermalink = permalink;
  if (!cleanPermalink.empty() && cleanPermalink.back() == '/') {
    cleanPermalink.pop_back();
  }
  std::string url = "https://www.reddit.com" + cleanPermalink + ".json?limit=5";
  const char *tempPath = "/apps/reddit/comments.tmp";
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr);
  if (result != HttpDownloader::OK) {
    RenderLock lock;
    Storage.remove(tempPath);
    return false;
  }

  HalFile file;
  bool opened = false;
  {
    RenderLock lock;
    opened = Storage.openFileForRead("REDDIT", tempPath, file);
  }
  if (!opened) {
    RenderLock lock;
    Storage.remove(tempPath);
    return false;
  }

  JsonDocument filter;
  filter[1]["data"]["children"][0]["kind"] = true;
  filter[1]["data"]["children"][0]["data"]["author"] = true;
  filter[1]["data"]["children"][0]["data"]["body"] = true;

  JsonDocument doc;
  DeserializationError err;
  {
    RenderLock lock;
    err = deserializeJson(doc, file, DeserializationOption::Filter(filter));
    file.close();
    Storage.remove(tempPath);
  }

  if (err)
    return false;

  if (!doc.is<JsonArray>())
    return false;
  auto arr = doc.as<JsonArray>();
  if (arr.size() < 2)
    return false;

  auto commentsListing = arr[1];
  if (commentsListing["data"]["children"].isNull())
    return false;
  auto children = commentsListing["data"]["children"].as<JsonArray>();

  comments.clear();
  for (JsonObject child : children) {
    if (child["kind"] != "t1")
      continue; // t1 is comment
    auto data = child["data"];
    RedditComment c;
    c.author = data["author"] | "";
    c.body = cleanText(data["body"] | "");
    comments.push_back(c);
  }
  return true;
}

void RedditActivity::performFetchPosts() {
  if (fetchTaskHandle != nullptr) return;
  isRefreshing = true;
  backgroundFetchFailed = false;
  pendingUpdatePosts = false;

  xTaskCreate(redditFetchTaskFunc, "red_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void RedditActivity::performFetchComments() {
  errorMessage.clear();
  const auto &post = posts[commentLoadIndex];
  ensureWifiConnected();
  if (WiFi.status() == WL_CONNECTED) {
    DownloadWatchdog::start(15000);
    bool success = fetchComments(post.permalink);
    DownloadWatchdog::stop();
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("REDDIT", "Comment fetch timed out! Going home.");
      activityManager.goHome();
      return;
    }
    if (success) {
      saveCommentsCache(post.id);
      offlineMode = false;
      return;
    }
  }

  // Offline fallback
  if (loadCommentsCache(post.id)) {
    offlineMode = true;
  } else {
    offlineMode = true;
    comments.clear();
    errorMessage = "Offline. No cached comments for this post.";
  }
}

void RedditActivity::runBackgroundFetch() {
  DownloadWatchdog::start(25000);
  errorMessage.clear();
  ensureWifiConnected();
  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    success = fetchPosts();
    if (success) {
      for (const auto &post : fetchedPosts) {
        if (fetchTaskHandle == nullptr) {
          break;
        }
        ensureWifiConnected();
        if (WiFi.status() != WL_CONNECTED)
          break;
        if (fetchComments(post.permalink)) {
          saveCommentsCache(post.id);
        }
        delay(100);
      }
    }
  }
  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("REDDIT", "Background fetch timed out!");
    backgroundFetchFailed = true;
    errorMessage = "Refresh timed out.";
  } else if (!success) {
    backgroundFetchFailed = true;
  } else {
    backgroundFetchFailed = false;
    errorMessage.clear();
  }

  pendingUpdatePosts = true;
}

void RedditActivity::loop() {

  if (pendingUpdatePosts) {
    pendingUpdatePosts = false;
    isRefreshing = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("REDDIT", "Watchdog timeout! Crashing to home screen.");
      activityManager.goHome();
      return;
    }
    if (!backgroundFetchFailed) {
      posts = std::move(fetchedPosts);
      savePostsCache();
      saveHomeMarkdown();
      offlineMode = false;
      selectedPostIndex = 0;
      postsScrollOffset = 0;
      if (state == RedditState::LoadingPosts) {
        state = RedditState::PostList;
      }
    } else {
      if (posts.empty()) {
        state = RedditState::LoadingPosts;
        if (loadOfflineFeeds()) {
          state = RedditState::PostList;
          offlineMode = true;
        } else {
          offlineMode = true;
          errorMessage = "Offline. No cached posts found.";
        }
      }
    }
    requestUpdate();
  }

  if (pendingFetch) {
    performFetchPosts();
    pendingFetch = false;
    requestUpdate();
    return;
  }

  if (state == RedditState::LoadingPosts && shouldFetchPosts) {
    performFetchPosts();
    shouldFetchPosts = false;
    requestUpdate();
    return;
  }

  if (state == RedditState::LoadingComments && shouldFetchComments) {
    performFetchComments();
    shouldFetchComments = false;
    if (!DownloadWatchdog::gotTimeout) {
      state = RedditState::CommentsList;
      selectedCommentIndex = 0;
      commentsScrollOffset = 0;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == RedditState::CommentsList) {
      state = RedditState::PostList;
      requestUpdate();
    } else if (state == RedditState::ManageSubscriptions) {
      state = RedditState::PostList;
      selectedPostIndex = -1;
      selectedHeaderAction = 1;
      if (subscriptionsChanged) {
        subscriptionsChanged = false;
        loadOfflineFeeds();
        state = RedditState::PostList;
        selectedPostIndex = 0;
        postsScrollOffset = 0;
        isRefreshing = true;
        pendingFetch = true;
      }
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == RedditState::PostList) {
    if (posts.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = RedditState::ManageSubscriptions;
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
        if (selectedPostIndex == -1) {
          selectedPostIndex = posts.size() - 1;
        } else if (selectedPostIndex == 0) {
          selectedPostIndex = -1;   // Select top refresh header bar
          selectedHeaderAction = 0; // Default to Refresh
        } else {
          selectedPostIndex--;
        }
        // Scrolling calculation
        if (selectedPostIndex != -1) {
          if (selectedPostIndex < postsScrollOffset) {
            postsScrollOffset = selectedPostIndex;
          }
        }
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (selectedPostIndex == -1) {
          selectedPostIndex = 0;
        } else if (selectedPostIndex == static_cast<int>(posts.size()) - 1) {
          selectedPostIndex = -1;   // Select top refresh header bar
          selectedHeaderAction = 0; // Default to Refresh
        } else {
          selectedPostIndex++;
        }
        // Scrolling calculation
        if (selectedPostIndex != -1) {
          if (selectedPostIndex >= postsScrollOffset + 5) {
            postsScrollOffset = selectedPostIndex - 4;
          }
        } else {
          postsScrollOffset = 0;
        }
        requestUpdate();
      } else if (selectedPostIndex == -1 &&
                 mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        if (selectedHeaderAction > 0) {
          selectedHeaderAction--;
          requestUpdate();
        }
      } else if (selectedPostIndex == -1 &&
                 mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        if (selectedHeaderAction < 2) {
          selectedHeaderAction++;
          requestUpdate();
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (selectedPostIndex == -1) {
          if (selectedHeaderAction == 0) {
            isRefreshing = true;
            pendingFetch = true;
            requestUpdate();
          } else if (selectedHeaderAction == 1) {
            state = RedditState::ManageSubscriptions;
            selectedSubIndex = 0;
            requestUpdate();
          } else if (selectedHeaderAction == 2) {
            activityManager.pushActivity(
                std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
          }
        } else if (!posts.empty()) {
          commentLoadIndex = selectedPostIndex;
          state = RedditState::LoadingComments;
          shouldFetchComments = true;
          requestUpdate();
        }
      }
    }
  } else if (state == RedditState::CommentsList) {
    if (comments.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
          mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        state = RedditState::LoadingComments;
        shouldFetchComments = true;
        requestUpdate();
        return;
      }
    } else {
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (selectedCommentIndex > 0) {
          selectedCommentIndex--;
          if (selectedCommentIndex < commentsScrollOffset) {
            commentsScrollOffset = selectedCommentIndex;
          }
          requestUpdate();
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (selectedCommentIndex < static_cast<int>(comments.size()) - 1) {
          selectedCommentIndex++;
          if (selectedCommentIndex >= commentsScrollOffset + 4) {
            commentsScrollOffset = selectedCommentIndex - 3;
          }
          requestUpdate();
        }
      }
    }
  } else if (state == RedditState::ManageSubscriptions) {
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
            renderer, mappedInput, "Add Subreddit", "", 25);
        startActivityForResult(
            std::move(keyboard), [this](const ActivityResult &result) {
              if (!result.isCancelled) {
                auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
                if (keyboardResult && !keyboardResult->text.empty()) {
                  std::string sub = sanitizeSubreddit(keyboardResult->text);
                  if (!sub.empty()) {
                    if (std::find(subscriptions.begin(), subscriptions.end(),
                                  sub) == subscriptions.end()) {
                      subscriptions.push_back(sub);
                      saveSubscriptions();
                      subscriptionsChanged = true;
                    }
                  }
                }
              }
              requestUpdate();
            });
      } else {
        std::string subToDelete = subscriptions[selectedSubIndex - 1];
        auto handler = [this, subToDelete](const ActivityResult &res) {
          if (!res.isCancelled) {
            auto it = std::find(subscriptions.begin(), subscriptions.end(),
                                subToDelete);
            if (it != subscriptions.end()) {
              subscriptions.erase(it);
              saveSubscriptions();
              subscriptionsChanged = true;
              selectedSubIndex = 0;
            }
          }
          requestUpdate();
        };
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(
                renderer, mappedInput, "Unsubscribe?", "r/" + subToDelete),
            handler);
      }
    }
  }
}

void RedditActivity::render(RenderLock &&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto &metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Reddit");

  if (state == RedditState::PostList && isRefreshing) {
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding,
                      metrics.topPadding + 5, "Refreshing feed...", true,
                      EpdFontFamily::REGULAR);
  }

  const int contentTop =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = contentBottom - contentTop;

  if (state == RedditState::LoadingPosts) {
    int textY = contentTop + contentHeight / 2 -
                renderer.getLineHeight(UI_12_FONT_ID) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading posts...");
    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == RedditState::LoadingComments) {
    int textY = contentTop + contentHeight / 2 -
                renderer.getLineHeight(UI_12_FONT_ID) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading comments...");
    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == RedditState::PostList) {
    // Draw Top Bar (Refresh, Subreddits, and WiFi buttons)
    const int statusY = contentTop;
    const int statusH = 32;
    const int statusW = (pageWidth - 2 * metrics.contentSidePadding - 24) / 3;
    const int statusX1 = metrics.contentSidePadding;
    const int statusX2 = statusX1 + statusW + 12;
    const int statusX3 = statusX2 + statusW + 12;

    bool headerSelected = (selectedPostIndex == -1);
    bool refreshSelected = (headerSelected && selectedHeaderAction == 0);
    bool subsSelected = (headerSelected && selectedHeaderAction == 1);
    bool wifiSelected = (headerSelected && selectedHeaderAction == 2);

    // Refresh Button
    renderer.drawRoundedRect(statusX1, statusY, statusW, statusH, 1, 6, true);
    if (refreshSelected) {
      renderer.fillRoundedRect(statusX1, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string refText = offlineMode ? "Offline" : "Refresh";
    int refTxtW = renderer.getTextWidth(SMALL_FONT_ID, refText.c_str());
    int refTxtX = statusX1 + (statusW - refTxtW) / 2;
    int refTxtY =
        statusY + (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, refTxtX, refTxtY, refText.c_str(),
                      !refreshSelected);

    // Subreddits Button
    renderer.drawRoundedRect(statusX2, statusY, statusW, statusH, 1, 6, true);
    if (subsSelected) {
      renderer.fillRoundedRect(statusX2, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string subsText = "Subs";
    int subsTxtW = renderer.getTextWidth(SMALL_FONT_ID, subsText.c_str());
    int subsTxtX = statusX2 + (statusW - subsTxtW) / 2;
    int subsTxtY =
        statusY + (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, subsTxtX, subsTxtY, subsText.c_str(),
                      !subsSelected);

    // WiFi Button
    renderer.drawRoundedRect(statusX3, statusY, statusW, statusH, 1, 6, true);
    if (wifiSelected) {
      renderer.fillRoundedRect(statusX3, statusY, statusW, statusH, 6,
                               Color::Black);
    }
    std::string wifiText = "WiFi";
    int wifiTxtW = renderer.getTextWidth(SMALL_FONT_ID, wifiText.c_str());
    int wifiTxtX = statusX3 + (statusW - wifiTxtW) / 2;
    int wifiTxtY =
        statusY + (statusH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, wifiTxtX, wifiTxtY, wifiText.c_str(),
                      !wifiSelected);

    if (!errorMessage.empty()) {
      int errY = statusY + statusH + 40;
      renderer.drawCenteredText(UI_10_FONT_ID, errY, errorMessage.c_str(), true,
                                EpdFontFamily::BOLD);
    } else {
      // Draw posts
      const int listTop = statusY + statusH + 12;
      const int cellH = 115;
      const int spacing = 10;

      for (int i = 0; i < 5; i++) {
        int idx = postsScrollOffset + i;
        if (idx >= static_cast<int>(posts.size()))
          break;

        const auto &post = posts[idx];
        int cellY = listTop + i * (cellH + spacing);
        int cellX = metrics.contentSidePadding;
        int cellW = pageWidth - 2 * metrics.contentSidePadding;

        bool isSelected = (idx == selectedPostIndex);
        renderer.drawRoundedRect(cellX, cellY, cellW, cellH, isSelected ? 3 : 1,
                                 8, true);

        // Subreddit and author
        std::string subAuthor = "r/" + post.subreddit + " • u/" + post.author;
        renderer.drawText(SMALL_FONT_ID, cellX + 12, cellY + 12,
                          subAuthor.c_str(), true, EpdFontFamily::BOLD);

        // Post Title (wrapped)
        auto titleLines =
            renderer.wrappedText(SMALL_FONT_ID, post.title.c_str(), cellW - 24,
                                 2, EpdFontFamily::REGULAR);
        for (size_t l = 0; l < titleLines.size(); l++) {
          renderer.drawText(SMALL_FONT_ID, cellX + 12, cellY + 34 + l * 20,
                            titleLines[l].c_str(), true,
                            EpdFontFamily::REGULAR);
        }

        // Comments Count
        std::string commText = std::to_string(post.numComments) + " comments";
        int commW = renderer.getTextWidth(SMALL_FONT_ID, commText.c_str());
        renderer.drawText(SMALL_FONT_ID, cellX + cellW - 12 - commW,
                          cellY + cellH - 24, commText.c_str(), true,
                          EpdFontFamily::REGULAR);
      }
    }

    if (posts.empty()) {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "WiFi",
                                                "Subscriptions", "Refresh");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      const auto labels = mappedInput.mapLabels(
          tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == RedditState::CommentsList) {
    const auto &post = posts[commentLoadIndex];

    // Context Post Card at the top
    const int postCardX = metrics.contentSidePadding;
    const int postCardY = contentTop;
    const int postCardW = pageWidth - 2 * metrics.contentSidePadding;
    const int postCardH = 75;

    renderer.drawRoundedRect(postCardX, postCardY, postCardW, postCardH, 1, 8,
                             true);
    std::string subAuthor = "r/" + post.subreddit + " • Context";
    renderer.drawText(SMALL_FONT_ID, postCardX + 12, postCardY + 10,
                      subAuthor.c_str(), true, EpdFontFamily::BOLD);
    auto titleLines =
        renderer.wrappedText(SMALL_FONT_ID, post.title.c_str(), postCardW - 24,
                             2, EpdFontFamily::REGULAR);
    for (size_t l = 0; l < titleLines.size(); l++) {
      renderer.drawText(SMALL_FONT_ID, postCardX + 12, postCardY + 28 + l * 18,
                        titleLines[l].c_str(), true, EpdFontFamily::REGULAR);
    }

    if (!errorMessage.empty()) {
      int errY = postCardY + postCardH + 40;
      renderer.drawCenteredText(UI_10_FONT_ID, errY, errorMessage.c_str(), true,
                                EpdFontFamily::BOLD);
    } else if (comments.empty()) {
      int errY = postCardY + postCardH + 40;
      renderer.drawCenteredText(UI_10_FONT_ID, errY, "No comments found.", true,
                                EpdFontFamily::BOLD);
    } else {
      // Draw comments list
      const int commentsTop = postCardY + postCardH + 15;
      const int cellH = 110;
      const int spacing = 8;

      for (int i = 0; i < 4; i++) {
        int idx = commentsScrollOffset + i;
        if (idx >= static_cast<int>(comments.size()))
          break;

        const auto &comment = comments[idx];
        int cellY = commentsTop + i * (cellH + spacing);
        int cellX = metrics.contentSidePadding;
        int cellW = pageWidth - 2 * metrics.contentSidePadding;

        bool isSelected = (idx == selectedCommentIndex);
        renderer.drawRoundedRect(cellX, cellY, cellW, cellH, isSelected ? 3 : 1,
                                 6, true);

        // Author
        std::string authorLabel = "u/" + comment.author;
        renderer.drawText(SMALL_FONT_ID, cellX + 10, cellY + 10,
                          authorLabel.c_str(), true, EpdFontFamily::BOLD);

        // Body
        auto bodyLines =
            renderer.wrappedText(SMALL_FONT_ID, comment.body.c_str(),
                                 cellW - 20, 3, EpdFontFamily::REGULAR);
        for (size_t l = 0; l < bodyLines.size(); l++) {
          renderer.drawText(SMALL_FONT_ID, cellX + 10, cellY + 30 + l * 20,
                            bodyLines[l].c_str(), true, EpdFontFamily::REGULAR);
        }
      }
    }

    if (comments.empty()) {
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), "WiFi", "Refresh", nullptr);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      const auto labels = mappedInput.mapLabels(
          tr(STR_BACK), nullptr, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == RedditState::ManageSubscriptions) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Subreddits");

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
            return std::string("[+ Add Subreddit]");
          return "r/" + subscriptions[index - 1];
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
