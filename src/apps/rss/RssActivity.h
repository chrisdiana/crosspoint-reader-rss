#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>

struct RssItem {
  std::string title;
  std::string link;
  std::string feedName;
};

enum class RssState {
  Loading,
  FeedList,
  ItemDetail,
  ManageSubscriptions
};

class RssActivity final : public Activity {
 private:
  RssState state = RssState::Loading;
  std::vector<RssItem> items;
  std::vector<std::string> subscriptions;

  int selectedItemIndex = 0;
  int selectedSubIndex = 0;
  int selectedHeaderAction = 0; // 0 = Refresh, 1 = Subscriptions
  int itemsScrollOffset = 0;

  bool isRefreshing = false;
  bool pendingFetch = false;
  bool offlineMode = false;
  bool wifiConnecting = false;
  bool pendingUpdateItems = false;
  bool backgroundFetchFailed = false;
  std::vector<RssItem> fetchedItems;
  void* fetchTaskHandle = nullptr; // TaskHandle_t
  std::string errorMessage;

  void loadSubscriptions();
  void saveSubscriptions();
  void saveCache();
  bool loadCache();
  void saveMarkdown();

  bool fetchFeeds();
  void performFetch();
  void ensureWifiConnected();

 public:
  void runBackgroundFetch();
  explicit RssActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RSS Feeds", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
