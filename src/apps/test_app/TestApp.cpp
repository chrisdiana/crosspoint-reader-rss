#include "TestApp.h"
#include "TestAppActivity.h"

std::unique_ptr<Activity> TestApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<TestAppActivity>(name, renderer, mappedInput);
}
