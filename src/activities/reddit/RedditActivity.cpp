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
#include "SilentRestart.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/WifiConnectHelper.h"
#include "activities/util/DownloadWatchdog.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <Bitmap.h>

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

std::string cleanBodyText(const std::string &input) {
  std::string output = input;
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
  return output;
}

std::string sanitizeFilename(const std::string& input) {
  std::string output = "";
  for (char c : input) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-') {
      output += c;
    }
  }
  if (output.length() > 50) {
    output = output.substr(0, 50);
  }
  while (!output.empty() && output.back() == ' ') {
    output.pop_back();
  }
  if (output.empty()) {
    output = "webpage";
  }
  return output;
}

bool isExternalUrl(const RedditPost& post) {
  if (post.postUrl.empty()) return false;
  if (post.permalink.length() > 5 && post.postUrl.find(post.permalink) != std::string::npos) {
    return false;
  }
  if (!post.imageUrl.empty()) {
    return false;
  }
  if (post.postUrl.rfind("http", 0) != 0) {
    return false;
  }
  return true;
}

bool hasSelectableUrl(const RedditPost& post) {
  return isExternalUrl(post);
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
  pendingUpdateComments = false;
  isFetchCommentsTask = false;
  fetchTaskHandle = nullptr;

  activeSubreddit = "";
  state = RedditState::SubredditList;
  selectedSubIndex = 0;
  selectedPostIndex = 0;
  postsScrollOffset = 0;
  isRefreshing = false;
  pendingFetch = false;
  requestUpdate();
}



void RedditActivity::onExit() {
  Activity::onExit();
  {
    RenderLock lock;
    Storage.remove("/apps/reddit/image.bmp");
    Storage.remove("/apps/reddit/image.tmp");
  }
  DownloadWatchdog::stop();
  if (fetchTaskHandle != nullptr) {
    cancelFetch = true;
    int waitCount = 0;
    while (fetchTaskHandle != nullptr && waitCount < 50) {
      delay(10);
      waitCount++;
    }
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (wifiWasUsed) {
    silentRestart();
  }
}

void RedditActivity::savePostsCache() {
  std::string sub = activeSubreddit;
  if (sub.empty()) sub = "home";
  saveSubredditMarkdown(sub, posts);
}

bool RedditActivity::loadPostsCache() {
  posts.clear();
  std::string sub = activeSubreddit;
  if (sub.empty()) sub = "home";
  bool res = parsePostsFromMarkdown("/apps/reddit/" + sub + ".md", posts);
  if (res) {
    std::sort(posts.begin(), posts.end(),
              [](const RedditPost &a, const RedditPost &b) {
                return a.ups > b.ups;
              });
  }
  return res;
}

void RedditActivity::saveSubredditMarkdown(const std::string &subName, const std::vector<RedditPost> &postsList) {
  std::string filepath = "/apps/reddit/" + subName + ".md";
  String md = "# " + String(subName.c_str()) + " Feed\n\n";
  for (const auto &post : postsList) {
    md += "## " + String(post.title.c_str()) + "\n";
    md += "- ID: " + String(post.id.c_str()) + "\n";
    md += "- Sub: " + String(post.subreddit.c_str()) + "\n";
    md += "- Author: " + String(post.author.c_str()) + "\n";
    md += "- Link: " + String(post.permalink.c_str()) + "\n";
    md += "- Comments: " + String(post.numComments) + "\n";
    md += "- Score: " + String(post.ups) + "\n";
    md += "- Timestamp: " + String(post.timestamp.c_str()) + "\n";
    md += "- ImageUrl: " + String(post.imageUrl.c_str()) + "\n";
    md += "- PostUrl: " + String(post.postUrl.c_str()) + "\n";
    std::string escapedSelftext = post.selftext;
    size_t npos = 0;
    while ((npos = escapedSelftext.find("\n", npos)) != std::string::npos) {
      escapedSelftext.replace(npos, 1, "\\n");
      npos += 2;
    }
    md += "- Selftext: " + String(escapedSelftext.c_str()) + "\n\n";
  }
  Storage.writeFile(filepath.c_str(), md);
}

bool RedditActivity::parsePostsFromMarkdown(const std::string &filepath, std::vector<RedditPost> &targetList) {
  HalFile file;
  if (!Storage.openFileForRead("REDDIT", filepath, file)) {
    return false;
  }

  std::string line = "";
  RedditPost currentPost;
  bool inPost = false;

  while (file.available() > 0) {
    char c = file.read();
    if (c == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (line.rfind("## ", 0) == 0) {
        if (inPost && !currentPost.id.empty()) {
          targetList.push_back(currentPost);
        }
        currentPost = RedditPost();
        currentPost.title = line.substr(3);
        inPost = true;
      } else if (inPost) {
        if (line.rfind("- ID: ", 0) == 0) {
          currentPost.id = line.substr(6);
        } else if (line.rfind("- Sub: ", 0) == 0) {
          currentPost.subreddit = line.substr(7);
        } else if (line.rfind("- Author: ", 0) == 0) {
          currentPost.author = line.substr(10);
        } else if (line.rfind("- Link: ", 0) == 0) {
          currentPost.permalink = line.substr(8);
        } else if (line.rfind("- Comments: ", 0) == 0) {
          currentPost.numComments = std::atoi(line.substr(12).c_str());
        } else if (line.rfind("- Score: ", 0) == 0) {
          currentPost.ups = std::atoi(line.substr(9).c_str());
        } else if (line.rfind("- Timestamp: ", 0) == 0) {
          currentPost.timestamp = line.substr(13);
        } else if (line.rfind("- ImageUrl: ", 0) == 0) {
          currentPost.imageUrl = line.substr(12);
        } else if (line.rfind("- PostUrl: ", 0) == 0) {
          currentPost.postUrl = line.substr(11);
        } else if (line.rfind("- Selftext: ", 0) == 0) {
          std::string rawSelftext = line.substr(12);
          size_t npos = 0;
          while ((npos = rawSelftext.find("\\n", npos)) != std::string::npos) {
            rawSelftext.replace(npos, 2, "\n");
            npos += 1;
          }
          currentPost.selftext = rawSelftext;
        }
      }
      line = "";
    } else {
      line += c;
    }
  }

  if (!line.empty()) {
    if (line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("## ", 0) == 0) {
      if (inPost && !currentPost.id.empty()) {
        targetList.push_back(currentPost);
      }
      currentPost = RedditPost();
      currentPost.title = line.substr(3);
      inPost = true;
    } else if (inPost) {
      if (line.rfind("- ID: ", 0) == 0) {
        currentPost.id = line.substr(6);
      } else if (line.rfind("- Sub: ", 0) == 0) {
        currentPost.subreddit = line.substr(7);
      } else if (line.rfind("- Author: ", 0) == 0) {
        currentPost.author = line.substr(10);
      } else if (line.rfind("- Link: ", 0) == 0) {
        currentPost.permalink = line.substr(8);
      } else if (line.rfind("- Comments: ", 0) == 0) {
        currentPost.numComments = std::atoi(line.substr(12).c_str());
      } else if (line.rfind("- Score: ", 0) == 0) {
        currentPost.ups = std::atoi(line.substr(9).c_str());
      } else if (line.rfind("- Timestamp: ", 0) == 0) {
        currentPost.timestamp = line.substr(13);
      } else if (line.rfind("- ImageUrl: ", 0) == 0) {
        currentPost.imageUrl = line.substr(12);
      } else if (line.rfind("- PostUrl: ", 0) == 0) {
        currentPost.postUrl = line.substr(11);
      } else if (line.rfind("- Selftext: ", 0) == 0) {
        std::string rawSelftext = line.substr(12);
        size_t npos = 0;
        while ((npos = rawSelftext.find("\\n", npos)) != std::string::npos) {
          rawSelftext.replace(npos, 2, "\n");
          npos += 1;
        }
        currentPost.selftext = rawSelftext;
      }
    }
  }

  if (inPost && !currentPost.id.empty()) {
    targetList.push_back(currentPost);
  }
  file.close();
  return !targetList.empty();
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

    if (!post.selftext.empty()) {
      md += "> " + String(post.selftext.c_str()) + "\n\n";
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
  filter["data"]["children"][0]["data"]["created_utc"] = true;
  filter["data"]["children"][0]["data"]["selftext"] = true;
  filter["data"]["children"][0]["data"]["url"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();

  if (err) {
    errorMessage = "Invalid JSON data.";
    return false;
  }

  if (doc["data"]["children"].isNull()) {
    if (doc["error"].is<int>()) {
      errorMessage = "Reddit Error: " + std::to_string(doc["error"].as<int>());
    } else if (doc["reason"].is<std::string>()) {
      errorMessage = "Subreddit: " + doc["reason"].as<std::string>();
    } else {
      errorMessage = "No posts found in subreddit.";
    }
    return false;
  }
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
    p.timestamp = std::to_string(static_cast<long long>(data["created_utc"] | 0.0));
    p.selftext = cleanBodyText(data["selftext"] | "");
    p.imageUrl = "";
    p.postUrl = data["url"] | "";
    if (p.postUrl.length() >= 4) {
      std::string ext = p.postUrl.substr(p.postUrl.length() - 4);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".jpg" || ext == ".png" || p.postUrl.find(".jpeg") != std::string::npos) {
        p.imageUrl = p.postUrl;
      }
    }
    targetList.push_back(p);
  }
  return true;
}

bool RedditActivity::loadOfflineFeeds() {
  posts.clear();
  if (subscriptions.empty()) {
    return parsePostsFromMarkdown("/apps/reddit/home.md", posts);
  }

  std::vector<std::vector<RedditPost>> subPostsLists;
  for (const auto &sub : subscriptions) {
    std::vector<RedditPost> subPosts;
    std::string filepath = "/apps/reddit/" + sub + ".md";
    if (parsePostsFromMarkdown(filepath, subPosts)) {
      if (!subPosts.empty()) {
        std::sort(subPosts.begin(), subPosts.end(),
                  [](const RedditPost &a, const RedditPost &b) {
                    return atoll(a.timestamp.c_str()) > atoll(b.timestamp.c_str());
                  });
        subPostsLists.push_back(subPosts);
      }
    }
  }

  if (subPostsLists.empty()) {
    return parsePostsFromMarkdown("/apps/reddit/home.md", posts);
  }

  for (const auto &list : subPostsLists) {
    posts.insert(posts.end(), list.begin(), list.end());
  }

  std::sort(posts.begin(), posts.end(),
            [](const RedditPost &a, const RedditPost &b) {
              return atoll(a.timestamp.c_str()) > atoll(b.timestamp.c_str());
            });

  return !posts.empty();
}

bool RedditActivity::fetchPostsFromUrl(const std::string &url,
                                       std::vector<RedditPost> &targetList,
                                       const std::string &cachePath) {
  const char *tempPath = "/apps/reddit/posts.tmp";
  std::string errorDetail;
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr, &cancelFetch, "", "", nullptr, nullptr, &errorDetail);
  if (result == HttpDownloader::ABORTED) {
    Storage.remove(tempPath);
    return false;
  }
  if (result != HttpDownloader::OK) {
    RenderLock lock;
    errorMessage = "Posts HTTP Error " + std::to_string(result) + ": " + (errorDetail.empty() ? "Unknown" : errorDetail);
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
  {
    RenderLock lock;
    fetchedPosts = posts;
  }

  std::string sub = activeSubreddit;
  if (sub.empty()) {
    sub = "home";
  }

  std::string url;
  std::string cachePath = "/apps/reddit/" + sub + ".json";

  if (sub == "home") {
    url = "https://www.reddit.com/.json?limit=10";
  } else {
    url = "https://www.reddit.com/r/" + sub + ".json?limit=10";
  }

  if (WiFi.status() != WL_CONNECTED)
    return false;

  std::vector<RedditPost> subPosts;
  if (fetchPostsFromUrl(url, subPosts, cachePath)) {
    if (!subPosts.empty()) {
      std::sort(subPosts.begin(), subPosts.end(),
                [](const RedditPost &a, const RedditPost &b) {
                  return a.ups > b.ups;
                });
      saveSubredditMarkdown(sub, subPosts);

      {
        RenderLock lock;
        fetchedPosts = std::move(subPosts);
        posts = fetchedPosts;
        if (selectedPostIndex >= static_cast<int>(posts.size())) {
          selectedPostIndex = posts.empty() ? -1 : posts.size() - 1;
        }
      }
      requestUpdate();
      Storage.remove(cachePath.c_str());
      return true;
    }
    Storage.remove(cachePath.c_str());
  }

  return false;
}

bool RedditActivity::fetchComments(const std::string &permalink) {
  std::string cleanPermalink = permalink;
  if (!cleanPermalink.empty() && cleanPermalink.back() == '/') {
    cleanPermalink.pop_back();
  }
  std::string url = "https://www.reddit.com" + cleanPermalink + ".json?limit=5";
  const char *tempPath = "/apps/reddit/comments.tmp";
  std::string errorDetail;
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr, &cancelFetch, "", "", nullptr, nullptr, &errorDetail);
  if (result == HttpDownloader::ABORTED) {
    Storage.remove(tempPath);
    return false;
  }
  if (result != HttpDownloader::OK) {
    RenderLock lock;
    errorMessage = "Comments HTTP Error " + std::to_string(result) + ": " + (errorDetail.empty() ? "Unknown" : errorDetail);
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
  filter[0]["data"]["children"][0]["kind"] = true;
  filter[0]["data"]["children"][0]["data"]["author"] = true;
  filter[0]["data"]["children"][0]["data"]["body"] = true;

  JsonDocument doc;
  DeserializationError err;
  {
    RenderLock lock;
    err = deserializeJson(doc, file, DeserializationOption::Filter(filter),
                          DeserializationOption::NestingLimit(30));
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
  cancelFetch = false;
  isRefreshing = true;
  backgroundFetchFailed = false;
  pendingUpdatePosts = false;
  isFetchCommentsTask = false;

  xTaskCreate(redditFetchTaskFunc, "red_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void RedditActivity::performFetchComments() {
  if (fetchTaskHandle != nullptr) return;
  cancelFetch = false;
  errorMessage.clear();
  backgroundFetchFailed = false;
  pendingUpdateComments = false;
  isFetchCommentsTask = true;
  isRefreshing = true;

  xTaskCreate(redditFetchTaskFunc, "red_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void RedditActivity::runBackgroundFetch() {
  DownloadWatchdog::start(35000);
  errorMessage.clear();

  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!WifiConnectHelper::waitForTimeSync()) {
      errorMessage = "Clock sync failed. HTTPS requests may fail.";
    }
    int fetchRetries = 3;
    while (fetchRetries > 0 && !cancelFetch) {
      if (isFetchCommentsTask) {
        const auto &post = posts[commentLoadIndex];
        success = fetchComments(post.permalink);
        if (success) {
          offlineMode = false;
          loadedImageHeight = 0;

          if (!post.imageUrl.empty()) {
            std::string imgTempPath = "/apps/reddit/image.tmp";
            std::string imgBmpPath = "/apps/reddit/image.bmp";
            {
              RenderLock lock;
              Storage.remove(imgTempPath.c_str());
              Storage.remove(imgBmpPath.c_str());
            }

            std::string imgErrorDetail;
            auto imgResult = HttpDownloader::downloadToFile(post.imageUrl.c_str(), imgTempPath.c_str(), nullptr, &cancelFetch, "", "", nullptr, nullptr, &imgErrorDetail);
            if (imgResult == HttpDownloader::ABORTED) {
              Storage.remove(imgTempPath.c_str());
              break;
            }

            if (imgResult == HttpDownloader::OK) {
              bool convertSuccess = false;
              int imgH = 0;
              {
                RenderLock lock;
                HalFile inFile, outFile;
                if (Storage.openFileForRead("REDDIT", imgTempPath, inFile)) {
                  if (Storage.openFileForWrite("REDDIT", imgBmpPath, outFile)) {
                    if (post.imageUrl.find(".png") != std::string::npos || post.imageUrl.find(".PNG") != std::string::npos) {
                      convertSuccess = PngToBmpConverter::pngFileToBmpStreamWithSize(inFile, outFile, 400, 200);
                    } else {
                      convertSuccess = JpegToBmpConverter::jpegFileToBmpStreamWithSize(inFile, outFile, 400, 200);
                    }
                    outFile.close();
                  }
                  inFile.close();
                }
                Storage.remove(imgTempPath.c_str());

                if (convertSuccess) {
                  HalFile checkFile;
                  if (Storage.openFileForRead("REDDIT", imgBmpPath, checkFile)) {
                    Bitmap checkBmp(checkFile, true);
                    if (checkBmp.parseHeaders() == BmpReaderError::Ok) {
                      imgH = checkBmp.getHeight();
                    }
                    checkFile.close();
                  }
                }
              }
              if (convertSuccess && imgH > 0) {
                loadedImageHeight = imgH;
              } else {
                RenderLock lock;
                Storage.remove(imgBmpPath.c_str());
              }
            } else {
              errorMessage = "Image HTTP Error " + std::to_string(imgResult) + ": " + (imgErrorDetail.empty() ? "Unknown" : imgErrorDetail);
            }
          }
        } else {
          offlineMode = false;
          comments.clear();
          if (errorMessage.empty()) {
            errorMessage = "Failed to load comments.";
          }
        }
      } else {
        if (fetchPosts()) {
          success = true;
        }
      }
      if (success || cancelFetch) {
        break;
      }
      fetchRetries--;
      if (fetchRetries > 0 && !cancelFetch) {
        LOG_DBG("REDDIT", "Reddit fetch failed, retrying in 1.5s...");
        delay(1500);
      }
    }
  } else {
    LOG_ERR("REDDIT", "WiFi failed to connect in background fetch.");
    if (isFetchCommentsTask) {
      offlineMode = true;
      comments.clear();
      errorMessage = "Offline. Connect to WiFi to fetch comments.";
    } else {
      errorMessage = "WiFi disconnected during fetch.";
    }
  }
  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("REDDIT", "Background fetch timed out!");
    backgroundFetchFailed = true;
    errorMessage = "Refresh timed out.";
  } else if (!success) {
    backgroundFetchFailed = true;
    if (errorMessage.empty()) {
      errorMessage = isFetchCommentsTask ? "Failed to load comments." : "Failed to fetch posts.";
    }
  } else {
    backgroundFetchFailed = false;
    if (!isFetchCommentsTask) {
      errorMessage.clear();
    }
  }

  if (cancelFetch) {
    fetchTaskHandle = nullptr;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  if (isFetchCommentsTask) {
    pendingUpdateComments = true;
  } else {
    pendingUpdatePosts = true;
  }
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
      {
        RenderLock lock;
        posts = std::move(fetchedPosts);
      }
      savePostsCache();
      offlineMode = false;
      if (state == RedditState::LoadingPosts) {
        selectedPostIndex = 0;
        postsScrollOffset = 0;
        state = RedditState::PostList;
      } else {
        RenderLock lock;
        if (selectedPostIndex >= static_cast<int>(posts.size())) {
          selectedPostIndex = posts.empty() ? -1 : posts.size() - 1;
        }
      }
    } else {
      if (posts.empty()) {
        state = RedditState::PostList;
        offlineMode = true;
        errorMessage = "Offline. No cached posts found.";
      }
    }
    requestUpdate();
  }

  if (pendingUpdateComments) {
    pendingUpdateComments = false;
    isRefreshing = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("REDDIT", "Watchdog timeout! Crashing to home screen.");
      activityManager.goHome();
      return;
    }
    if (!backgroundFetchFailed) {
      state = RedditState::CommentsList;
      selectedCommentIndex = -1;
      commentsScrollOffset = 0;
    } else {
      state = RedditState::PostList;
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
    if (fetchTaskHandle != nullptr) {
      return; // Wait for previous task to cancel
    }
    performFetchPosts();
    shouldFetchPosts = false;
    requestUpdate();
    return;
  }

  if (state == RedditState::LoadingComments && shouldFetchComments) {
    if (fetchTaskHandle != nullptr) {
      return; // Wait for previous task to cancel
    }
    performFetchComments();
    shouldFetchComments = false;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == RedditState::CommentsList || state == RedditState::LoadingComments) {
      if (fetchTaskHandle != nullptr) {
        cancelFetch = true;
      } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      isRefreshing = false;
      state = RedditState::PostList;
      requestUpdate();
    } else if (state == RedditState::PostList || state == RedditState::LoadingPosts) {
      if (fetchTaskHandle != nullptr) {
        cancelFetch = true;
      } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      isRefreshing = false;
      pendingFetch = false;
      activeSubreddit = "";
      posts.clear();
      state = RedditState::SubredditList;
      requestUpdate();
    } else if (state == RedditState::SubredditList) {
      finish();
    }
    return;
  }

  if (state == RedditState::SubredditList) {
    int totalItems = static_cast<int>(subscriptions.size()) + 2; // Homepage + subscriptions + Add Subreddit
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedSubIndex = (selectedSubIndex - 1 + totalItems) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedSubIndex = (selectedSubIndex + 1) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedSubIndex == totalItems - 1) {
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
                    activeSubreddit = sub;
                    bool hasCache = loadPostsCache();
                    selectedPostIndex = 0;
                    postsScrollOffset = 0;
                    errorMessage = "";
                    if (hasCache) {
                      state = RedditState::PostList;
                      isRefreshing = true;
                      requestUpdate();
                      ensureWifiConnected([this]() {
                        pendingFetch = true;
                        requestUpdate();
                      }, [this]() {
                        isRefreshing = false;
                        requestUpdate();
                      });
                    } else {
                      state = RedditState::LoadingPosts;
                      isRefreshing = true;
                      requestUpdate();
                      ensureWifiConnected([this]() {
                        pendingFetch = true;
                        requestUpdate();
                      }, [this]() {
                        state = RedditState::SubredditList;
                        isRefreshing = false;
                        requestUpdate();
                      });
                    }
                    return;
                  }
                }
              }
              requestUpdate();
            });
      } else {
        if (selectedSubIndex == 0) {
          activeSubreddit = "home";
        } else {
          activeSubreddit = subscriptions[selectedSubIndex - 1];
        }
        bool hasCache = loadPostsCache();
        selectedPostIndex = 0;
        postsScrollOffset = 0;
        errorMessage = "";
        if (hasCache) {
          state = RedditState::PostList;
          isRefreshing = true;
          requestUpdate();
          ensureWifiConnected([this]() {
            pendingFetch = true;
            requestUpdate();
          }, [this]() {
            isRefreshing = false;
            requestUpdate();
          });
        } else {
          state = RedditState::LoadingPosts;
          isRefreshing = true;
          requestUpdate();
          ensureWifiConnected([this]() {
            pendingFetch = true;
            requestUpdate();
          }, [this]() {
            state = RedditState::SubredditList;
            isRefreshing = false;
            requestUpdate();
          });
        }
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (selectedSubIndex > 0 && selectedSubIndex < totalItems - 1) {
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
  } else if (state == RedditState::PostList) {
    RenderLock lock;
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      isRefreshing = true;
      state = RedditState::LoadingPosts;
      requestUpdate();
      ensureWifiConnected([this]() {
        pendingFetch = true;
        requestUpdate();
      }, [this]() {
        state = RedditState::PostList;
        isRefreshing = false;
        requestUpdate();
      });
      return;
    }
    if (posts.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        isRefreshing = true;
        state = RedditState::LoadingPosts;
        requestUpdate();
        ensureWifiConnected([this]() {
          pendingFetch = true;
          requestUpdate();
        }, [this]() {
          state = RedditState::PostList;
          isRefreshing = false;
          requestUpdate();
        });
        return;
      }
    } else {
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (selectedPostIndex > 0) {
          selectedPostIndex--;
          if (selectedPostIndex < postsScrollOffset) {
            postsScrollOffset = selectedPostIndex;
          }
          requestUpdate();
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        if (selectedPostIndex < static_cast<int>(posts.size()) - 1) {
          selectedPostIndex++;
          if (selectedPostIndex >= postsScrollOffset + 5) {
            postsScrollOffset = selectedPostIndex - 4;
          }
          requestUpdate();
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        commentLoadIndex = selectedPostIndex;
        state = RedditState::LoadingComments;
        requestUpdate();
        ensureWifiConnected([this]() {
          shouldFetchComments = true;
          requestUpdate();
        }, [this]() {
          state = RedditState::PostList;
          requestUpdate();
        });
      }
    }
  } else if (state == RedditState::CommentsList) {
    if (comments.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.pushActivity(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Up) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        state = RedditState::LoadingComments;
        selectedCommentIndex = -1;
        commentsScrollOffset = 0;
        requestUpdate();
        ensureWifiConnected([this]() {
          shouldFetchComments = true;
          requestUpdate();
        }, [this]() {
          state = RedditState::CommentsList;
          requestUpdate();
        });
        return;
      }
    } else {
      const auto pageWidth = renderer.getScreenWidth();
      const auto pageHeight = renderer.getScreenHeight();
      const auto &metrics = UITheme::getInstance().getMetrics();
      const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
      const int postCardW = pageWidth - 2 * metrics.contentSidePadding;

      const auto &post = posts[commentLoadIndex];
      auto titleLines = renderer.wrappedText(SMALL_FONT_ID, post.title.c_str(), postCardW - 24, 9999, EpdFontFamily::BOLD);
      std::vector<std::string> bodyLines;
      if (!post.selftext.empty()) {
        bodyLines = renderer.wrappedText(SMALL_FONT_ID, post.selftext.c_str(), postCardW - 24, 9999, EpdFontFamily::REGULAR);
      }
      int titleHeight = titleLines.size() * 20;
      int bodyHeight = bodyLines.size() * 20;
      int imgSpacing = 0;
      if (loadedImageHeight > 0) {
        imgSpacing = loadedImageHeight + 8;
        if (bodyHeight == 0) {
          imgSpacing += 12;
        }
      }
      bool hasUrl = hasSelectableUrl(post);
      int urlSpacing = hasUrl ? 40 : 0;
      int postCardH = 12 + 20 + titleHeight + (bodyHeight > 0 ? bodyHeight + 15 : 0) + imgSpacing + urlSpacing + 12;

      int spacing = 8;
      int cellH = 110;
      int totalHeight = postCardH + spacing + comments.size() * (cellH + spacing);
      int viewportHeight = contentBottom - contentTop;
      int maxScrollOffset = std::max(0, totalHeight - viewportHeight);

      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        if (selectedCommentIndex > -1) {
          selectedCommentIndex--;
          
          if (selectedCommentIndex == -1) {
            // Selected post card!
            if (hasUrl) {
              int btnTop = contentTop + postCardH - 12 - 32;
              int btnBottom = btnTop + 32;
              int relBtnTop = btnTop - commentsScrollOffset;
              int relBtnBottom = btnBottom - commentsScrollOffset;
              if (relBtnTop < contentTop) {
                commentsScrollOffset -= (contentTop - relBtnTop);
              } else if (relBtnBottom > contentBottom) {
                commentsScrollOffset += (relBtnBottom - contentBottom);
              }
            } else {
              commentsScrollOffset = 0;
            }
          } else {
            int cellY = contentTop + postCardH + spacing + selectedCommentIndex * (cellH + spacing);
            int relCellTop = cellY - commentsScrollOffset;
            if (relCellTop < contentTop) {
              commentsScrollOffset -= (contentTop - relCellTop);
            }
          }
          requestUpdate();
        } else if (selectedCommentIndex == -1) {
          if (commentsScrollOffset > 0) {
            commentsScrollOffset = std::max(0, commentsScrollOffset - 100);
            requestUpdate();
          }
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        int maxSelIndex = static_cast<int>(comments.size()) - 1;
        if (selectedCommentIndex < maxSelIndex) {
          if (selectedCommentIndex == -1) {
            int postCardBottom = contentTop + postCardH;
            int relPostCardBottom = postCardBottom - commentsScrollOffset;
            if (relPostCardBottom > contentBottom) {
              commentsScrollOffset = std::min(commentsScrollOffset + 100, maxScrollOffset);
            } else {
              selectedCommentIndex = 0;
              int cellY = contentTop + postCardH + spacing + selectedCommentIndex * (cellH + spacing);
              int relCellBottom = cellY + cellH - commentsScrollOffset;
              if (relCellBottom > contentBottom) {
                commentsScrollOffset += (relCellBottom - contentBottom);
              }
            }
          } else {
            selectedCommentIndex++;
            int cellY = contentTop + postCardH + spacing + selectedCommentIndex * (cellH + spacing);
            int relCellBottom = cellY + cellH - commentsScrollOffset;
            if (relCellBottom > contentBottom) {
              commentsScrollOffset += (relCellBottom - contentBottom);
            }
          }
          requestUpdate();
        } else if (selectedCommentIndex == maxSelIndex) {
          if (commentsScrollOffset < maxScrollOffset) {
            commentsScrollOffset = std::min(commentsScrollOffset + 100, maxScrollOffset);
            requestUpdate();
          }
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (hasUrl && selectedCommentIndex == -1) {
          std::string sanitized = sanitizeFilename(post.title);
          if (sanitized.length() > 30) {
            sanitized = sanitized.substr(0, 30);
          }
          ensureWifiConnected([this, post, sanitized]() {
            GUI.drawPopup(renderer, "Loading Page...");
            renderer.displayBuffer(HalDisplay::FAST_REFRESH);
            
            Storage.ensureDirectoryExists("/websites");
            std::string tempPath = "/websites/temp_download.tmp";
            
            std::string contentType;
            std::string finalUrl;
            
            DownloadWatchdog::start(20000);
            auto res = HttpDownloader::downloadToFile(post.postUrl.c_str(), tempPath.c_str(), nullptr, nullptr, "", "", &contentType, &finalUrl);
            DownloadWatchdog::stop();
            
            if (res == HttpDownloader::OK) {
              std::string ext = ".html";
              std::string urlToCheck = finalUrl.empty() ? post.postUrl : finalUrl;
              
              std::string contentTypeLower = contentType;
              for (char &c : contentTypeLower) c = tolower(c);
              
              size_t queryPos = urlToCheck.find('?');
              if (queryPos != std::string::npos) {
                urlToCheck = urlToCheck.substr(0, queryPos);
              }
              
              bool isTxt = false;
              if (contentTypeLower.find("text/plain") != std::string::npos) {
                isTxt = true;
              } else if (urlToCheck.length() >= 4) {
                std::string urlExt = urlToCheck.substr(urlToCheck.length() - 4);
                for (char &c : urlExt) c = tolower(c);
                if (urlExt == ".txt") {
                  isTxt = true;
                }
              }
              
              if (isTxt) {
                ext = ".txt";
              }
              
              std::string destPath = "/websites/" + sanitized + ext;
              {
                RenderLock lock;
                if (Storage.exists(destPath.c_str())) {
                  Storage.remove(destPath.c_str());
                }
                Storage.rename(tempPath.c_str(), destPath.c_str());
              }
              activityManager.pushReader(destPath);
            } else {
              GUI.drawPopup(renderer, "Failed to load page.");
              renderer.displayBuffer(HalDisplay::HALF_REFRESH);
              delay(1500);
              requestUpdate();
            }
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
          }, [this]() {
            requestUpdate();
          });
          return;
        }
      }
    }
  }
}

void RedditActivity::render(RenderLock &&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto &metrics = UITheme::getInstance().getMetrics();

  std::string headerTitle = "Reddit";
  if (state == RedditState::PostList || state == RedditState::LoadingPosts) {
    if (activeSubreddit == "home") {
      headerTitle = "Reddit - Homepage";
    } else {
      headerTitle = "r/" + activeSubreddit;
    }
  }

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 headerTitle.c_str());

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
    if (!errorMessage.empty()) {
      int errY = contentTop + 40;
      renderer.drawCenteredText(UI_10_FONT_ID, errY, errorMessage.c_str(), true,
                                EpdFontFamily::BOLD);
    } else {
      // Draw posts
      const int listTop = contentTop;
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

    const auto labels = mappedInput.mapLabels(
        tr(STR_BACK), tr(STR_SELECT), nullptr, "Refresh");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == RedditState::CommentsList) {
    const auto &post = posts[commentLoadIndex];
    bool hasUrl = hasSelectableUrl(post);

    const int postCardX = metrics.contentSidePadding;
    const int postCardW = pageWidth - 2 * metrics.contentSidePadding;

    std::string subAuthor = "r/" + post.subreddit + " • u/" + post.author;
    auto titleLines =
        renderer.wrappedText(SMALL_FONT_ID, post.title.c_str(), postCardW - 24,
                             9999, EpdFontFamily::BOLD);

    std::vector<std::string> bodyLines;
    if (!post.selftext.empty()) {
      bodyLines = renderer.wrappedText(SMALL_FONT_ID, post.selftext.c_str(), postCardW - 24,
                                       9999, EpdFontFamily::REGULAR);
    }

    int titleHeight = titleLines.size() * 20;
    int bodyHeight = bodyLines.size() * 20;
    int imgSpacing = 0;
    if (loadedImageHeight > 0) {
      imgSpacing = loadedImageHeight + 8;
      if (bodyHeight == 0) {
        imgSpacing += 12;
      }
    }
    int urlSpacing = hasUrl ? 40 : 0;
    int postCardH = 12 + 20 + titleHeight + (bodyHeight > 0 ? bodyHeight + 15 : 0) + imgSpacing + urlSpacing + 12;

    int spacing = 8;
    int cellH = 110;
    int totalHeight = postCardH + spacing + comments.size() * (cellH + spacing);
    int viewportHeight = contentBottom - contentTop;
    int maxScrollOffset = std::max(0, totalHeight - viewportHeight);

    // Clamp commentsScrollOffset
    commentsScrollOffset = std::max(0, std::min(commentsScrollOffset, maxScrollOffset));

    // Draw the post card (clipped to contentTop / contentBottom)
    int currentPostCardY = contentTop - commentsScrollOffset;
    if (currentPostCardY + postCardH > contentTop && currentPostCardY < contentBottom) {
      renderer.fillRoundedRect(postCardX, currentPostCardY, postCardW, postCardH, 8, (selectedCommentIndex == -1) ? Color::LightGray : Color::White);
      renderer.drawRoundedRect(postCardX, currentPostCardY, postCardW, postCardH, (selectedCommentIndex == -1) ? 3 : 1, 8, true);

      renderer.drawText(SMALL_FONT_ID, postCardX + 12, currentPostCardY + 10,
                        subAuthor.c_str(), true, EpdFontFamily::REGULAR);

      int currentY = currentPostCardY + 32;
      for (size_t l = 0; l < titleLines.size(); l++) {
        renderer.drawText(SMALL_FONT_ID, postCardX + 12, currentY,
                          titleLines[l].c_str(), true, EpdFontFamily::BOLD);
        currentY += 20;
      }

      if (bodyHeight > 0) {
        currentY += 8;
        renderer.drawLine(postCardX + 12, currentY, postCardX + postCardW - 12, currentY, 1, true);
        currentY += 12;
        for (size_t l = 0; l < bodyLines.size(); l++) {
          renderer.drawText(SMALL_FONT_ID, postCardX + 12, currentY,
                            bodyLines[l].c_str(), true, EpdFontFamily::REGULAR);
          currentY += 20;
        }
      }

      if (loadedImageHeight > 0) {
        currentY += 8;
        if (bodyHeight == 0) {
          renderer.drawLine(postCardX + 12, currentY, postCardX + postCardW - 12, currentY, 1, true);
          currentY += 12;
        }
        
        if (currentY + loadedImageHeight > contentTop && currentY < contentBottom) {
          HalFile imgFile;
          if (Storage.openFileForRead("REDDIT", "/apps/reddit/image.bmp", imgFile)) {
            Bitmap bitmap(imgFile, true);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              int imgX = postCardX + (postCardW - bitmap.getWidth()) / 2;
              renderer.drawBitmap(bitmap, imgX, currentY, postCardW, loadedImageHeight, 0, 0);
            }
            imgFile.close();
          }
        }
        currentY += loadedImageHeight;
      }

      if (hasUrl) {
        currentY += 8;
        bool isUrlSelected = (selectedCommentIndex == -1);
        renderer.drawRoundedRect(postCardX + 12, currentY, postCardW - 24, 32, isUrlSelected ? 3 : 1, 6, true);
        
        std::string btnText = "Open: " + post.postUrl;
        if (btnText.length() > 48) {
          btnText = btnText.substr(0, 45) + "...";
        }
        int txtW = renderer.getTextWidth(SMALL_FONT_ID, btnText.c_str());
        int txtX = postCardX + 12 + (postCardW - 24 - txtW) / 2;
        renderer.drawText(SMALL_FONT_ID, txtX, currentY + 8, btnText.c_str(), true, isUrlSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
    }

    // Draw comments
    int commentStart = contentTop + postCardH + spacing - commentsScrollOffset;
    if (!errorMessage.empty()) {
      int errY = std::max(contentTop, commentStart + 40);
      renderer.drawCenteredText(UI_10_FONT_ID, errY, errorMessage.c_str(), true,
                                EpdFontFamily::BOLD);
    } else if (comments.empty()) {
      int errY = std::max(contentTop, commentStart + 40);
      renderer.drawCenteredText(UI_10_FONT_ID, errY, "No comments found.", true,
                                EpdFontFamily::BOLD);
    } else {
      for (size_t idx = 0; idx < comments.size(); ++idx) {
        int cellY = commentStart + idx * (cellH + spacing);
        if (cellY + cellH <= contentTop || cellY >= contentBottom) {
          continue;
        }
        const auto &comment = comments[idx];
        int cellX = metrics.contentSidePadding;
        int cellW = pageWidth - 2 * metrics.contentSidePadding;

        bool isSelected = (static_cast<int>(idx) == selectedCommentIndex);
        renderer.fillRoundedRect(cellX, cellY, cellW, cellH, 8, isSelected ? Color::LightGray : Color::White);
        renderer.drawRoundedRect(cellX, cellY, cellW, cellH, isSelected ? 3 : 1, 6, true);

        // Author
        std::string authorLabel = "u/" + comment.author;
        renderer.drawText(SMALL_FONT_ID, cellX + 10, cellY + 10,
                          authorLabel.c_str(), true, EpdFontFamily::BOLD);

        // Body
        auto commLines =
            renderer.wrappedText(SMALL_FONT_ID, comment.body.c_str(),
                                 cellW - 20, 4, EpdFontFamily::REGULAR);
        for (size_t l = 0; l < commLines.size(); l++) {
          renderer.drawText(SMALL_FONT_ID, cellX + 10, cellY + 30 + l * 18,
                            commLines[l].c_str(), true, EpdFontFamily::REGULAR);
        }
      }
    }

    if (comments.empty()) {
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), "Subreddits", "Refresh", nullptr);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    } else {
      bool isUrlSelected = (hasUrl && selectedCommentIndex == -1);
      const auto labels = mappedInput.mapLabels(
          tr(STR_BACK), isUrlSelected ? tr(STR_SELECT) : nullptr, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                          labels.btn4);
    }
  } else if (state == RedditState::SubredditList) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Reddit");

    int totalItems = static_cast<int>(subscriptions.size()) + 2;

    GUI.drawButtonMenu(
        renderer,
        Rect{
            0,
            metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
            pageWidth,
            pageHeight - (metrics.headerHeight + metrics.topPadding +
                          metrics.verticalSpacing + metrics.buttonHintsHeight)},
        totalItems, selectedSubIndex,
        [this, totalItems](int index) {
          if (index == 0)
            return std::string("Homepage");
          if (index == totalItems - 1)
            return std::string("[+ Add Subreddit]");
          return "r/" + subscriptions[index - 1];
        },
        [this, totalItems](int index) {
          if (index == 0)
            return UIIcon::Library;
          if (index == totalItems - 1)
            return UIIcon::File;
          return UIIcon::Library;
        });

    const char* rightAction = nullptr;
    if (selectedSubIndex > 0 && selectedSubIndex < totalItems - 1) {
      rightAction = "Delete";
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                              nullptr, rightAction);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  }

  renderer.displayBuffer();
}
