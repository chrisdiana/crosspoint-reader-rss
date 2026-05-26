#include "WeatherApp.h"
#include "WeatherActivity.h"

std::unique_ptr<Activity> WeatherApp::createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<WeatherActivity>(renderer, mappedInput);
}
