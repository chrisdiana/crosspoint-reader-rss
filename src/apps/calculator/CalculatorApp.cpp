#include "CalculatorApp.h"
#include "CalculatorActivity.h"

std::unique_ptr<Activity> CalculatorApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<CalculatorActivity>(renderer, mappedInput);
}
