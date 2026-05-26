#include "ClockApp.h"
#include "ClockActivity.h"

std::unique_ptr<Activity> ClockApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ClockActivity>(renderer, mappedInput);
}
