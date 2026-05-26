#include "WikipediaApp.h"
#include "WikipediaActivity.h"

std::unique_ptr<Activity> WikipediaApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<WikipediaActivity>(renderer, mappedInput);
}
