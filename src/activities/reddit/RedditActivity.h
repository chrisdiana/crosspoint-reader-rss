#pragma once

#include "activities/Activity.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

struct RedditPost {
  std::string id;
  std::string title;
  std::string subreddit;
  std::string author;
  std::string permalink;
  int numComments = 0;
  int ups = 0;
  std::string timestamp; // Unix timestamp as string
  std::string selftext;  // Text body of the post
  std::string imageUrl;  // URL of the post's image, if it has one
  std::string postUrl;   // External web page URL, if it links to one
};

struct RedditComment {
  std::string author;
  std::string body;
};

enum class RedditState {
  SubredditList,
  LoadingPosts,
  PostList,
  LoadingComments,
  CommentsList
};

class RedditActivity final : public Activity {
 private:
  RedditState state = RedditState::SubredditList;
  std::vector<RedditPost> posts;
  std::vector<RedditComment> comments;
  std::vector<std::string> subscriptions;
  std::string activeSubreddit;

  int selectedPostIndex = 0;
  int selectedCommentIndex = 0;
  int selectedSubIndex = 0;
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
  bool pendingUpdateComments = false;
  bool backgroundFetchFailed = false;
  bool isFetchCommentsTask = false;
  std::vector<RedditPost> fetchedPosts;
  void* fetchTaskHandle = nullptr;
  int commentLoadIndex = 0;
  int loadedImageHeight = 0;
  bool cancelFetch = false;
  bool wifiWasUsed = false;

  void savePostsCache();
  bool loadPostsCache();
  void saveCommentsCache(const std::string& id);
  bool loadCommentsCache(const std::string& id);
  void saveSubredditMarkdown(const std::string& subName, const std::vector<RedditPost>& postsList);
  bool parsePostsFromMarkdown(const std::string& filepath, std::vector<RedditPost>& targetList);

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

 public:
  void runBackgroundFetch();
  explicit RedditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Reddit", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
