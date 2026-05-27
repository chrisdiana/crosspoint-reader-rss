#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>

struct RedditPost {
  std::string id;
  std::string title;
  std::string subreddit;
  std::string author;
  std::string permalink;
  int numComments = 0;
  int ups = 0;
};

struct RedditComment {
  std::string author;
  std::string body;
};

enum class RedditState {
  LoadingPosts,
  PostList,
  LoadingComments,
  CommentsList,
  ManageSubscriptions
};

class RedditActivity final : public Activity {
 private:
  RedditState state = RedditState::LoadingPosts;
  std::vector<RedditPost> posts;
  std::vector<RedditComment> comments;
  std::vector<std::string> subscriptions;

  int selectedPostIndex = 0;
  int selectedCommentIndex = 0;
  int selectedSubIndex = 0;
  int selectedHeaderAction = 0; // 0 = Refresh, 1 = Subreddits
  int postsScrollOffset = 0;
  int commentsScrollOffset = 0;

  bool offlineMode = false;
  std::string errorMessage;
  bool shouldFetchPosts = false;
  bool shouldFetchComments = false;
  bool subscriptionsChanged = false;
  bool pendingFetch = false;
  bool isRefreshing = false;
  bool wifiConnecting = false;
  bool pendingUpdatePosts = false;
  bool backgroundFetchFailed = false;
  std::vector<RedditPost> fetchedPosts;
  void* fetchTaskHandle = nullptr;
  int commentLoadIndex = 0;

  void savePostsCache();
  bool loadPostsCache();
  void saveCommentsCache(const std::string& id);
  bool loadCommentsCache(const std::string& id);

  void loadSubscriptions();
  void saveSubscriptions();
  void saveHomeMarkdown();
  std::string sanitizeSubreddit(const std::string& input);

  bool parsePostsFromFile(const std::string& filepath, std::vector<RedditPost>& targetList);
  bool loadOfflineFeeds();
  bool fetchPostsFromUrl(const std::string& url, std::vector<RedditPost>& targetList, const std::string& cachePath = "");
  bool fetchPosts();
  bool fetchComments(const std::string& permalink);
  void performFetchPosts();
  void performFetchComments();
  void ensureWifiConnected();

 public:
  void runBackgroundFetch();
  explicit RedditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Reddit", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
