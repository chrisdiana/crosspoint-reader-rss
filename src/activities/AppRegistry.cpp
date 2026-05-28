#include "AppRegistry.h"
#include "activities/calculator/CalculatorActivity.h"
#include "activities/weather/WeatherActivity.h"
#include "activities/sudoku/SudokuActivity.h"
#include "activities/reddit/RedditActivity.h"
#include "activities/wikipedia/WikipediaActivity.h"
#include "activities/chess/ChessActivity.h"
#include "activities/dice/DiceActivity.h"
#include "activities/rss/RssActivity.h"

// System Activities
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/settings/OpdsServerListActivity.h"
#include "OpdsServerStore.h"
#include "I18n.h"

AppRegistry &AppRegistry::getInstance() {
  static AppRegistry instance;
  return instance;
}

AppRegistry::AppRegistry() {
  // Browse Files
  apps.push_back(std::make_unique<App>(
      []() { return tr(STR_BROWSE_FILES); },
      UIIcon::Folder,
      [](GfxRenderer& r, MappedInputManager& i) {
        return std::make_unique<FileBrowserActivity>(r, i);
      }));

  // Recent Books
  apps.push_back(std::make_unique<App>(
      []() { return tr(STR_MENU_RECENT_BOOKS); },
      UIIcon::Recent,
      [](GfxRenderer& r, MappedInputManager& i) {
        return std::make_unique<RecentBooksActivity>(r, i);
      }));

  // OPDS Browser (conditionally visible)
  apps.push_back(std::make_unique<App>(
      []() { return tr(STR_OPDS_BROWSER); },
      UIIcon::Library,
      [](GfxRenderer& r, MappedInputManager& i) -> std::unique_ptr<Activity> {
        const auto& servers = OPDS_STORE.getServers();
        if (servers.size() == 1) {
          return std::make_unique<OpdsBookBrowserActivity>(r, i, servers[0]);
        } else {
          return std::make_unique<OpdsServerListActivity>(r, i, true);
        }
      },
      []() { return OPDS_STORE.hasServers(); }));

  // File Transfer
  apps.push_back(std::make_unique<App>(
      []() { return tr(STR_FILE_TRANSFER); },
      UIIcon::Transfer,
      [](GfxRenderer& r, MappedInputManager& i) {
        return std::make_unique<CrossPointWebServerActivity>(r, i);
      }));

  // Settings
  apps.push_back(std::make_unique<App>(
      []() { return tr(STR_SETTINGS_TITLE); },
      UIIcon::Settings,
      [](GfxRenderer& r, MappedInputManager& i) {
        return std::make_unique<SettingsActivity>(r, i);
      }));

  // Calculator App
  apps.push_back(std::make_unique<App>("Calculator", UIIcon::Calculator, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<CalculatorActivity>(r, i);
  }));

  // Weather App
  apps.push_back(std::make_unique<App>("Weather", UIIcon::Weather, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<WeatherActivity>(r, i);
  }));

  // Sudoku App
  apps.push_back(std::make_unique<App>("Sudoku", UIIcon::Sudoku, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<SudokuActivity>(r, i);
  }));

  // Reddit App
  apps.push_back(std::make_unique<App>("Reddit", UIIcon::Reddit, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<RedditActivity>(r, i);
  }));

  // Wikipedia App
  apps.push_back(std::make_unique<App>("Wikipedia", UIIcon::Wikipedia, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<WikipediaActivity>(r, i);
  }));

  // Chess App
  apps.push_back(std::make_unique<App>("Chess", UIIcon::Chess, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<ChessActivity>(r, i);
  }));

  // Dice App
  apps.push_back(std::make_unique<App>("Dice", UIIcon::Dice, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<DiceActivity>(r, i);
  }));

  // RSS Feed App
  apps.push_back(std::make_unique<App>("RSS Feed", UIIcon::Library, [](GfxRenderer& r, MappedInputManager& i) {
    return std::make_unique<RssActivity>(r, i);
  }));
}
