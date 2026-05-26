#include "AppRegistry.h"
#include "calculator/CalculatorApp.h"
#include "reddit/RedditApp.h"
#include "sudoku/SudokuApp.h"
#include "test_app/TestApp.h"
#include "weather/WeatherApp.h"
#include "clock/ClockApp.h"
#include "rss/RssApp.h"
#include "wikipedia/WikipediaApp.h"
#include "minesweeper/MinesweeperApp.h"
#include "chess/ChessApp.h"
#include "dice/DiceApp.h"

AppRegistry &AppRegistry::getInstance() {
  static AppRegistry instance;
  return instance;
}

AppRegistry::AppRegistry() {
  // Parameterized Test Apps

  // Calculator App (demonstrates default fallback icon)
  apps.push_back(std::make_unique<CalculatorApp>());

  // Weather App (also uses default fallback icon)
  apps.push_back(std::make_unique<WeatherApp>());

  // Sudoku App
  apps.push_back(std::make_unique<SudokuApp>());

  // Reddit App
  apps.push_back(std::make_unique<RedditApp>());

  // Clock App
  apps.push_back(std::make_unique<ClockApp>());

  // RSS Feeds App
  apps.push_back(std::make_unique<RssApp>());

  // Wikipedia App
  apps.push_back(std::make_unique<WikipediaApp>());

  // Minesweeper App
  apps.push_back(std::make_unique<MinesweeperApp>());

  // Chess App
  apps.push_back(std::make_unique<ChessApp>());

  // Dice App
  apps.push_back(std::make_unique<DiceApp>());
}
