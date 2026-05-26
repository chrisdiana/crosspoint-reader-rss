#include "MinesweeperApp.h"
#include "MinesweeperActivity.h"

std::unique_ptr<Activity> MinesweeperApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<MinesweeperActivity>(renderer, mappedInput);
}
