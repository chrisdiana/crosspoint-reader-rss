#include "RssApp.h"
#include "RssActivity.h"

std::unique_ptr<Activity> RssApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<RssActivity>(renderer, mappedInput);
}
