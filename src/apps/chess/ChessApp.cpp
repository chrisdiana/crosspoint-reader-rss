#include "ChessApp.h"
#include "ChessActivity.h"

std::unique_ptr<Activity> ChessApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ChessActivity>(renderer, mappedInput);
}
