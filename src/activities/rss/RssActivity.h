#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>
#include <functional>

struct RssItem {
  std::string title;
  std::string link;
  std::string description;
  std::string timestamp; // Unix timestamp as string
  std::string feedName;  // display name of the feed
};

enum class RssState {
  FeedSelection,
  Loading,
  FeedList
};

class RssActivity final : public Activity {
 private:
   RssState state = RssState::FeedSelection;
   std::vector<RssItem> allItems;
   std::vector<std::string> subscriptions;
   std::string activeFeed;

   int selectedItemIndex = 0;
   int selectedSubIndex = 0;
   int itemsScrollOffset = 0;

   bool offlineMode = false;
   std::string errorMessage;
   bool isRefreshing = false;
   bool pendingUpdateFeed = false;
   bool backgroundFetchSuccess = false;
   void* fetchTaskHandle = nullptr;
   bool cancelFetch = false;
   bool wifiWasUsed = false;

   void loadSubscriptions();
   void saveSubscriptions();
   bool loadOfflineFeeds();
   void ensureDirectoriesExist();

   void saveFeedMarkdown(const std::string &feedName, const std::vector<RssItem> &itemsList);
   bool parseFeedsFromMarkdown(const std::string &filepath, std::vector<RssItem> &targetList);

 public:
  void runBackgroundFetch();
  explicit RssActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RssFeed", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
