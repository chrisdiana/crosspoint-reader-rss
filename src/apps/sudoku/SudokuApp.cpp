#include "SudokuApp.h"
#include "SudokuActivity.h"

std::unique_ptr<Activity> SudokuApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<SudokuActivity>(renderer, mappedInput);
}
