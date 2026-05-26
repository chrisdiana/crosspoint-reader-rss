#pragma once

#include "activities/Activity.h"
#include <string>

class MinesweeperActivity final : public Activity {
 private:
  bool mineGrid[9][9] = {false};
  bool uncoveredGrid[9][9] = {false};
  bool flagGrid[9][9] = {false};

  int cursorRow = 4;
  int cursorCol = 4;

  bool gameOver = false;
  bool gameWon = false;
  bool firstClick = true;
  bool flagMode = false; // false = Dig, true = Flag

  unsigned long gameStartTime = 0;
  unsigned long gameEndTime = 0;

  void initializeBoard(int firstRow, int firstCol);
  void uncoverCell(int r, int c);
  int countNeighborMines(int r, int c) const;
  bool checkWin() const;
  void revealAllMines();

 public:
  explicit MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Minesweeper", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
