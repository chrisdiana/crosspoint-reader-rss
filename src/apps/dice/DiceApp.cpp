#include "DiceApp.h"
#include "DiceActivity.h"

std::unique_ptr<Activity> DiceApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<DiceActivity>(renderer, mappedInput);
}
