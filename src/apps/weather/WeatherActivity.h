#pragma once

#include "activities/Activity.h"
#include <string>

class WeatherActivity final : public Activity {
 public:
  enum class WeatherState {
    Init,
    SelectCity,
    Loading,
    ShowWeather
  };

 private:
  WeatherState state = WeatherState::Init;
  int selectedCityIndex = 0;
  bool weatherLoaded = false;
  double temp = 0.0;
  double windspeed = 0.0;
  int weatherCode = 0;
  std::string timeStr;
  std::string cityName;
  bool offlineMode = false;
  std::string errorMessage;
  bool shouldFetch = false;

  void handleConfirm();
  void performFetch();

 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
