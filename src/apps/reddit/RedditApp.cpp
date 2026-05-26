#include "RedditApp.h"
#include "RedditActivity.h"

std::unique_ptr<Activity> RedditApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<RedditActivity>(renderer, mappedInput);
}
