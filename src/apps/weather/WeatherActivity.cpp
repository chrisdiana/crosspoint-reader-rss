#include "WeatherActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <cstdlib>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
struct City {
  const char *name;
  double lat;
  double lon;
};

const City CITIES[] = {
    {"New York", 40.7128, -74.0060}, {"London", 51.5074, -0.1278},
    {"Tokyo", 35.6762, 139.6503},    {"Paris", 48.8566, 2.3522},
    {"Sydney", -33.8688, 151.2093},  {"San Francisco", 37.7749, -122.4194},
    {"Berlin", 52.5200, 13.4050},    {"Toronto", 43.6532, -79.3832},
    {"Mumbai", 19.0760, 72.8777},    {"Cairo", 30.0444, 31.2357}};
const int CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

const char *getWeatherDesc(int code) {
  switch (code) {
  case 0:
    return "Clear sky";
  case 1:
    return "Mainly clear";
  case 2:
    return "Partly cloudy";
  case 3:
    return "Overcast";
  case 45:
  case 48:
    return "Foggy";
  case 51:
  case 53:
  case 55:
    return "Drizzle";
  case 61:
  case 63:
  case 65:
    return "Rainy";
  case 71:
  case 73:
  case 75:
    return "Snowy";
  case 80:
  case 81:
  case 82:
    return "Rain showers";
  case 95:
  case 96:
  case 99:
    return "Thunderstorm";
  default:
    return "Unknown";
  }
}

void saveConfig(int cityIndex, const std::string &cityName) {
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");
  JsonDocument doc;
  doc["city_index"] = cityIndex;
  doc["city_name"] = cityName;
  String output;
  serializeJson(doc, output);
  Storage.writeFile("/apps/weather/config.json", output);
}

bool loadConfig(int &cityIndex, std::string &cityName) {
  String input = Storage.readFile("/apps/weather/config.json");
  if (input.length() == 0)
    return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err)
    return false;
  cityIndex = doc["city_index"] | 0;
  cityName = doc["city_name"] | "";
  return true;
}

void saveCache(double temp, double windspeed, int weathercode,
               const std::string &timeStr) {
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");
  JsonDocument doc;
  doc["temp"] = temp;
  doc["windspeed"] = windspeed;
  doc["weathercode"] = weathercode;
  doc["time"] = timeStr;
  String output;
  serializeJson(doc, output);
  Storage.writeFile("/apps/weather/cache.json", output);
}

bool loadCache(double &temp, double &windspeed, int &weathercode,
               std::string &timeStr) {
  String input = Storage.readFile("/apps/weather/cache.json");
  if (input.length() == 0)
    return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err)
    return false;
  temp = doc["temp"] | 0.0;
  windspeed = doc["windspeed"] | 0.0;
  weathercode = doc["weathercode"] | 0;
  timeStr = doc["time"] | "";
  return true;
}

bool fetchWeather(double lat, double lon, double &temp, double &windspeed,
                  int &weathercode, std::string &timeStr) {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/"
           "forecast?latitude=%.4f&longitude=%.4f&current_weather=true",
           lat, lon);
  std::string response;
  if (!HttpDownloader::fetchUrl(url, response)) {
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err)
    return false;

  if (doc["current_weather"].isNull())
    return false;
  auto cw = doc["current_weather"];
  temp = cw["temperature"] | 0.0;
  windspeed = cw["windspeed"] | 0.0;
  weathercode = cw["weathercode"] | 0;
  timeStr = cw["time"] | "";
  return true;
}
} // namespace

void WeatherActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15) {
      delay(200);
      retries++;
    }
  }

  if (loadConfig(selectedCityIndex, cityName)) {
    state = WeatherState::Loading;
    shouldFetch = true;
  } else {
    state = WeatherState::SelectCity;
    selectedCityIndex = 0;
  }
  requestUpdate();
}

void WeatherActivity::onExit() { Activity::onExit(); }

void WeatherActivity::loop() {
  if (state == WeatherState::Loading && shouldFetch) {
    performFetch();
    shouldFetch = false;
    state = WeatherState::ShowWeather;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == WeatherState::SelectCity) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedCityIndex = (selectedCityIndex - 1 + CITY_COUNT) % CITY_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedCityIndex = (selectedCityIndex + 1) % CITY_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cityName = CITIES[selectedCityIndex].name;
      saveConfig(selectedCityIndex, cityName);
      state = WeatherState::Loading;
      shouldFetch = true;
      requestUpdate();
    }
  } else if (state == WeatherState::ShowWeather) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Manual refresh
      state = WeatherState::Loading;
      shouldFetch = true;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      // Change City
      state = WeatherState::SelectCity;
      requestUpdate();
    } else if ((!weatherLoaded || offlineMode) &&
               mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      // WiFi Settings
      activityManager.pushActivity(
          std::make_unique<WifiSelectionActivity>(renderer, mappedInput));
    }
  }
}

void WeatherActivity::performFetch() {
  errorMessage.clear();
  double lat = CITIES[selectedCityIndex].lat;
  double lon = CITIES[selectedCityIndex].lon;

  if (WiFi.status() == WL_CONNECTED) {
    if (fetchWeather(lat, lon, temp, windspeed, weatherCode, timeStr)) {
      saveCache(temp, windspeed, weatherCode, timeStr);
      offlineMode = false;
      weatherLoaded = true;
      return;
    }
  }

  // Fallback to cache
  if (loadCache(temp, windspeed, weatherCode, timeStr)) {
    offlineMode = true;
    weatherLoaded = true;
  } else {
    offlineMode = true;
    weatherLoaded = false;
    errorMessage = "No network & no cached data.";
  }
}

void WeatherActivity::render(RenderLock &&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto &metrics = UITheme::getInstance().getMetrics();

  if (state == WeatherState::SelectCity) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Select City");

    GUI.drawButtonMenu(
        renderer,
        Rect{
            0,
            metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
            pageWidth,
            pageHeight - (metrics.headerHeight + metrics.topPadding +
                          metrics.verticalSpacing + metrics.buttonHintsHeight)},
        CITY_COUNT, selectedCityIndex,
        [](int index) { return std::string(CITIES[index].name); },
        [](int index) { return UIIcon::Library; });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == WeatherState::Loading) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Weather");

    const int contentTop =
        metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom =
        pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int contentHeight = contentBottom - contentTop;

    int textY = contentTop + contentHeight / 2 -
                renderer.getLineHeight(UI_12_FONT_ID) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading weather...");

    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  } else if (state == WeatherState::ShowWeather) {
    GUI.drawHeader(renderer,
                   Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Weather");

    const int contentTop =
        metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom =
        pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int contentHeight = contentBottom - contentTop;

    const int cardX = metrics.contentSidePadding;
    const int cardW = pageWidth - 2 * metrics.contentSidePadding;
    const int cardH = 240;
    const int cardY = contentTop + (contentHeight - cardH) / 2;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 12, true);

    if (weatherLoaded) {
      int curY = cardY + 20;

      renderer.drawCenteredText(UI_12_FONT_ID, curY, cityName.c_str(), true,
                                EpdFontFamily::BOLD);
      curY += renderer.getLineHeight(UI_12_FONT_ID) + 15;

      const char *desc = getWeatherDesc(weatherCode);
      renderer.drawCenteredText(UI_10_FONT_ID, curY, desc, true,
                                EpdFontFamily::BOLD);
      curY += renderer.getLineHeight(UI_10_FONT_ID) + 15;

      char tempBuf[32];
      snprintf(tempBuf, sizeof(tempBuf), "%.1f °C", temp);
      renderer.drawCenteredText(UI_12_FONT_ID, curY, tempBuf, true,
                                EpdFontFamily::BOLD);
      curY += renderer.getLineHeight(UI_12_FONT_ID) + 20;

      char windBuf[64];
      snprintf(windBuf, sizeof(windBuf), "Wind: %.1f km/h", windspeed);
      renderer.drawCenteredText(SMALL_FONT_ID, curY, windBuf, true,
                                EpdFontFamily::REGULAR);
      curY += renderer.getLineHeight(SMALL_FONT_ID) + 10;

      char timeBuf[64];
      std::string formattedTime = timeStr;
      size_t tPos = formattedTime.find('T');
      if (tPos != std::string::npos) {
        formattedTime[tPos] = ' ';
      }
      snprintf(timeBuf, sizeof(timeBuf), "Updated: %s", formattedTime.c_str());
      renderer.drawCenteredText(SMALL_FONT_ID, curY, timeBuf, true,
                                EpdFontFamily::REGULAR);
      curY += renderer.getLineHeight(SMALL_FONT_ID) + 15;

      if (offlineMode) {
        renderer.drawCenteredText(SMALL_FONT_ID, curY, "Offline (Cached Data)",
                                  true, EpdFontFamily::REGULAR);
      } else {
        renderer.drawCenteredText(SMALL_FONT_ID, curY, "Online Mode", true,
                                  EpdFontFamily::REGULAR);
      }
    } else {
      int textY = cardY + cardH / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, textY, errorMessage.c_str(),
                                true, EpdFontFamily::BOLD);
    }

    const char *btn4Label = (!weatherLoaded || offlineMode) ? "WiFi" : nullptr;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Refresh",
                                              "Change City", btn4Label);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                        labels.btn4);
  }

  renderer.displayBuffer();
}
