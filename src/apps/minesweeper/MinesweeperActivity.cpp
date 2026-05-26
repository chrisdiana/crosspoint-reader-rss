#include "MinesweeperActivity.h"
#include <algorithm>
#include <GfxRenderer.h>
#include <I18n.h>
#include <cstdlib>
#include <Arduino.h>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MinesweeperActivity::onEnter() {
  Activity::onEnter();
  srand(millis());
  
  // Clear grids
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      mineGrid[r][c] = false;
      uncoveredGrid[r][c] = false;
      flagGrid[r][c] = false;
    }
  }

  cursorRow = 4;
  cursorCol = 4;
  gameOver = false;
  gameWon = false;
  firstClick = true;
  flagMode = false;
  gameStartTime = 0;
  gameEndTime = 0;

  requestUpdate();
}

void MinesweeperActivity::onExit() {
  Activity::onExit();
}

void MinesweeperActivity::initializeBoard(int firstRow, int firstCol) {
  // Clear grid
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      mineGrid[r][c] = false;
    }
  }

  // Place 10 mines
  int minesPlaced = 0;
  while (minesPlaced < 10) {
    int r = rand() % 9;
    int c = rand() % 9;
    // Safe zone around the first click (3x3 grid centered at click)
    if (abs(r - firstRow) <= 1 && abs(c - firstCol) <= 1) {
      continue;
    }
    if (!mineGrid[r][c]) {
      mineGrid[r][c] = true;
      minesPlaced++;
    }
  }

  gameStartTime = millis();
}

int MinesweeperActivity::countNeighborMines(int r, int c) const {
  int count = 0;
  for (int dr = -1; dr <= 1; dr++) {
    for (int dc = -1; dc <= 1; dc++) {
      if (dr == 0 && dc == 0) continue;
      int nr = r + dr;
      int nc = c + dc;
      if (nr >= 0 && nr < 9 && nc >= 0 && nc < 9) {
        if (mineGrid[nr][nc]) {
          count++;
        }
      }
    }
  }
  return count;
}

void MinesweeperActivity::uncoverCell(int r, int c) {
  if (r < 0 || r >= 9 || c < 0 || c >= 9) return;
  if (uncoveredGrid[r][c] || flagGrid[r][c]) return;

  uncoveredGrid[r][c] = true;

  if (countNeighborMines(r, c) == 0) {
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        if (dr == 0 && dc == 0) continue;
        uncoverCell(r + dr, c + dc);
      }
    }
  }
}

bool MinesweeperActivity::checkWin() const {
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      if (!mineGrid[r][c] && !uncoveredGrid[r][c]) {
        return false;
      }
    }
  }
  return true;
}

void MinesweeperActivity::revealAllMines() {
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      if (mineGrid[r][c]) {
        uncoveredGrid[r][c] = true;
      }
    }
  }
}

void MinesweeperActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (cursorRow == -1) {
    // Toolbar navigation
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      cursorCol = (cursorCol - 1 + 3) % 3;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      cursorCol = (cursorCol + 1) % 3;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      cursorRow = 0;
      cursorCol = 4;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (cursorCol == 0) {
        onEnter(); // Restart
      } else if (cursorCol == 1) {
        flagMode = false;
        cursorRow = 0;
        cursorCol = 4;
        requestUpdate();
      } else if (cursorCol == 2) {
        flagMode = true;
        cursorRow = 0;
        cursorCol = 4;
        requestUpdate();
      }
    }
  } else {
    // Grid navigation
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (cursorRow == 0) {
        cursorRow = -1;
        cursorCol = 0; // Highlight New Game
      } else {
        cursorRow--;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (cursorRow == 8) {
        cursorRow = -1;
        cursorCol = 0; // Highlight New Game
      } else {
        cursorRow++;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      cursorCol = (cursorCol - 1 + 9) % 9;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      cursorCol = (cursorCol + 1) % 9;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (gameOver) {
        onEnter(); // Restart on GameOver confirm
        return;
      }

      if (firstClick) {
        initializeBoard(cursorRow, cursorCol);
        firstClick = false;
      }

      if (flagMode) {
        if (!uncoveredGrid[cursorRow][cursorCol]) {
          flagGrid[cursorRow][cursorCol] = !flagGrid[cursorRow][cursorCol];
          requestUpdate();
        }
      } else {
        if (!flagGrid[cursorRow][cursorCol]) {
          if (mineGrid[cursorRow][cursorCol]) {
            gameOver = true;
            revealAllMines();
            gameEndTime = millis();
          } else {
            uncoverCell(cursorRow, cursorCol);
            if (checkWin()) {
              gameWon = true;
              gameOver = true;
              gameEndTime = millis();
            }
          }
          requestUpdate();
        }
      }
    }
  }
}

void MinesweeperActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Minesweeper");

  // Draw Toolbar
  const int toolbarY = metrics.topPadding + metrics.headerHeight + 20;
  
  // New Game Button
  bool newGameSel = (cursorRow == -1 && cursorCol == 0);
  renderer.drawRoundedRect(20, toolbarY, 120, 30, 1, 5, newGameSel);
  renderer.drawText(SMALL_FONT_ID, 30, toolbarY + 7, "New Game", !newGameSel);

  // Dig Mode Button
  bool digSel = (cursorRow == -1 && cursorCol == 1);
  bool digActive = !flagMode;
  renderer.drawRoundedRect(160, toolbarY, 110, 30, 1, 5, digSel || (digActive && cursorRow != -1));
  renderer.drawText(SMALL_FONT_ID, 175, toolbarY + 7, "Dig Mode", !(digSel || (digActive && cursorRow != -1)));

  // Flag Mode Button
  bool flagSel = (cursorRow == -1 && cursorCol == 2);
  bool flagActive = flagMode;
  renderer.drawRoundedRect(290, toolbarY, 110, 30, 1, 5, flagSel || (flagActive && cursorRow != -1));
  renderer.drawText(SMALL_FONT_ID, 305, toolbarY + 7, "Flag Mode", !(flagSel || (flagActive && cursorRow != -1)));

  // Grid constants
  const int cellS = 34; // 34x34 cells
  const int gridW = 9 * cellS;
  const int gridX = (pageWidth - gridW) / 2;
  const int gridY = toolbarY + 50;

  // Draw 9x9 cells
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      int cx = gridX + c * cellS;
      int cy = gridY + r * cellS;

      bool isCursor = (cursorRow == r && cursorCol == c);

      if (uncoveredGrid[r][c]) {
        if (mineGrid[r][c]) {
          // Draw Mine
          renderer.drawRect(cx, cy, cellS, cellS, 1, true);
          renderer.drawLine(cx + 4, cy + 4, cx + cellS - 5, cy + cellS - 5, 2, false);
          renderer.drawLine(cx + cellS - 5, cy + 4, cx + 4, cy + cellS - 5, 2, false);
        } else {
          // Draw Empty / Number
          renderer.drawRect(cx, cy, cellS, cellS, 1, true);
          int neighbors = countNeighborMines(r, c);
          if (neighbors > 0) {
            std::string nStr = std::to_string(neighbors);
            int nW = renderer.getTextWidth(SMALL_FONT_ID, nStr.c_str());
            int nH = renderer.getTextHeight(SMALL_FONT_ID);
            renderer.drawText(SMALL_FONT_ID, cx + (cellS - nW) / 2, cy + (cellS - nH) / 2, nStr.c_str(), false, EpdFontFamily::BOLD);
          }
        }
      } else {
        // Covered Cell
        renderer.drawRect(cx, cy, cellS, cellS, 1, true);
        renderer.drawRoundedRect(cx + 2, cy + 2, cellS - 4, cellS - 4, 1, 3, false);

        if (flagGrid[r][c]) {
          // Draw Flag
          renderer.drawLine(cx + cellS / 2 - 2, cy + 8, cx + cellS / 2 - 2, cy + cellS - 8, 2, true);
          renderer.drawRect(cx + cellS / 2 - 2, cy + 8, 10, 6, 1, true);
        }
      }

      if (isCursor) {
        // Draw double highlight border around cursor cell
        renderer.drawRect(cx - 2, cy - 2, cellS + 4, cellS + 4, 2, true);
      }
    }
  }

  // Draw Footer Status
  const int footerY = gridY + gridW + 20;
  
  // Calculate flags placed
  int flagsPlaced = 0;
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      if (flagGrid[r][c]) flagsPlaced++;
    }
  }

  unsigned long elapsed = 0;
  if (firstClick) {
    elapsed = 0;
  } else if (gameOver) {
    elapsed = (gameEndTime - gameStartTime) / 1000;
  } else {
    elapsed = (millis() - gameStartTime) / 1000;
  }

  if (gameWon) {
    std::string winStr = "VICTORY! Time: " + std::to_string(elapsed) + "s";
    renderer.drawCenteredText(UI_12_FONT_ID, footerY, winStr.c_str(), true, EpdFontFamily::BOLD);
  } else if (gameOver) {
    renderer.drawCenteredText(UI_12_FONT_ID, footerY, "GAME OVER - Hit a mine!", true, EpdFontFamily::BOLD);
  } else {
    std::string statStr = "Mines: 10 | Flags: " + std::to_string(flagsPlaced) + " | Time: " + std::to_string(elapsed) + "s";
    renderer.drawCenteredText(SMALL_FONT_ID, footerY, statStr.c_str(), true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
