#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>

enum class WikiState {
  OfflineList,
  SearchResults,
  Loading,
  ArticleView
};

class WikipediaActivity final : public Activity {
 private:
  WikiState state = WikiState::OfflineList;
  std::vector<std::string> offlineArticles;
  std::vector<std::string> searchResults;

  std::string currentArticleTitle;
  std::string currentArticleText;
  std::vector<std::string> articleLines;

  int selectedIndex = 0;
  int listScrollOffset = 0;
  int articleScrollOffset = 0; // lines scrolled down in ArticleView

  bool pendingSearch = false;
  bool pendingArticle = false;
  bool wifiConnecting = false;
  std::string searchQuery;
  std::string articleToFetch;
  std::string errorMessage;

  void loadOfflineArticlesList();
  void ensureWifiConnected();
  void doSearch();
  void doFetchArticle();

 public:
  explicit WikipediaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wikipedia", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
