#include "DiceActivity.h"
#include <algorithm>
#include <GfxRenderer.h>
#include <I18n.h>
#include <cstdlib>
#include <cmath>
#include <Arduino.h>
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Correct Midpoint Circle helper (Bresenham's)
static void drawCircle(const GfxRenderer& renderer, int x0, int y0, int radius, int thickness, bool state) {
  for (int t = 0; t < thickness; t++) {
    int r = radius - t;
    int x = r;
    int y = 0;
    int err = 3 - 2 * r;
    while (x >= y) {
      renderer.drawPixel(x0 + x, y0 + y, state);
      renderer.drawPixel(x0 + y, y0 + x, state);
      renderer.drawPixel(x0 - y, y0 + x, state);
      renderer.drawPixel(x0 - x, y0 + y, state);
      renderer.drawPixel(x0 - x, y0 - y, state);
      renderer.drawPixel(x0 - y, y0 - x, state);
      renderer.drawPixel(x0 + y, y0 - x, state);
      renderer.drawPixel(x0 + x, y0 - y, state);
      
      if (err >= 0) {
        x -= 1;
        err += 4 * (y - x) + 10;
      } else {
        err += 4 * y + 6;
      }
      y += 1;
    }
  }
}

static void fillCircle(const GfxRenderer& renderer, int x0, int y0, int radius, bool state) {
  int x = radius;
  int y = 0;
  int err = 3 - 2 * radius;
  while (x >= y) {
    renderer.drawLine(x0 - x, y0 + y, x0 + x, y0 + y, state);
    renderer.drawLine(x0 - y, y0 + x, x0 + y, y0 + x, state);
    renderer.drawLine(x0 - x, y0 - y, x0 + x, y0 - y, state);
    renderer.drawLine(x0 - y, y0 - x, x0 + y, y0 - x, state);
    if (err >= 0) {
      x -= 1;
      err += 4 * (y - x) + 10;
    } else {
      err += 4 * y + 6;
    }
    y += 1;
  }
}

void DiceActivity::onEnter() {
  Activity::onEnter();
  srand(millis());
  roll();
  requestUpdate();
}

void DiceActivity::onExit() {
  Activity::onExit();
}

void DiceActivity::roll() {
  switch (currentMode) {
    case DiceMode::D6:
      lastRollD6 = 1 + (rand() % 6);
      break;
    case DiceMode::Coin:
      lastRollCoinHeads = (rand() % 2 == 0);
      break;
    case DiceMode::Random10:
      lastRollRandom10 = rand() % 11;
      break;
    case DiceMode::Arrow:
      lastRollArrowDirection = rand() % 8;
      break;
    case DiceMode::D20:
      lastRollD20 = 1 + (rand() % 20);
      break;
  }
}

void DiceActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    currentMode = static_cast<DiceMode>((static_cast<int>(currentMode) - 1 + 5) % 5);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    currentMode = static_cast<DiceMode>((static_cast<int>(currentMode) + 1) % 5);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    roll();
    requestUpdate();
  }
}

void DiceActivity::drawD6(int x, int y, int size, int value) {
  const char* dieStr = "";
  switch (value) {
    case 1: dieStr = "\u2680"; break;
    case 2: dieStr = "\u2681"; break;
    case 3: dieStr = "\u2682"; break;
    case 4: dieStr = "\u2683"; break;
    case 5: dieStr = "\u2684"; break;
    case 6: dieStr = "\u2685"; break;
  }
  int tW = renderer.getTextWidth(NOTOSANS_48_EMOJI_FONT_ID, dieStr);
  int tH = renderer.getTextHeight(NOTOSANS_48_EMOJI_FONT_ID);
  renderer.drawText(NOTOSANS_48_EMOJI_FONT_ID, x - tW / 2, y - tH / 2, dieStr, true);
}

void DiceActivity::drawCoin(int x, int y, int size, bool heads) {
  int radius = size / 2;
  // Double circle outline
  drawCircle(renderer, x, y, radius, 1, true);
  fillCircle(renderer, x, y, radius - 4, false);

  std::string txt = heads ? "HEADS" : "TAILS";
  int tW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, txt.c_str());
  int tH = renderer.getTextHeight(NOTOSANS_18_FONT_ID);
  renderer.drawText(NOTOSANS_18_FONT_ID, x - tW / 2, y - tH / 2, txt.c_str(), true, EpdFontFamily::BOLD);

  // Decorative coin star
  fillCircle(renderer, x, y - radius / 2, 4, true);
  fillCircle(renderer, x, y + radius / 2, 4, true);
}

void DiceActivity::drawArrow(int x, int y, int size, int direction) {
  const char* arrowStr = "";
  switch (direction) {
    case 0: arrowStr = "\u2191"; break; // Up
    case 1: arrowStr = "\u2197"; break; // Up-Right
    case 2: arrowStr = "\u2192"; break; // Right
    case 3: arrowStr = "\u2198"; break; // Down-Right
    case 4: arrowStr = "\u2193"; break; // Down
    case 5: arrowStr = "\u2199"; break; // Down-Left
    case 6: arrowStr = "\u2190"; break; // Left
    case 7: arrowStr = "\u2196"; break; // Up-Left
  }
  int tW = renderer.getTextWidth(NOTOSANS_48_EMOJI_FONT_ID, arrowStr);
  int tH = renderer.getTextHeight(NOTOSANS_48_EMOJI_FONT_ID);
  renderer.drawText(NOTOSANS_48_EMOJI_FONT_ID, x - tW / 2, y - tH / 2, arrowStr, true);
}

void DiceActivity::drawD20(int x, int y, int size, int value) {
  int radius = size / 2;
  
  // Draw 20-sided icosahedron outline (Hexagon shape) using optimized single-precision float math
  int px[6];
  int py[6];
  for (int i = 0; i < 6; i++) {
    float angle = (i * 60.0f - 90.0f) * (float)M_PI / 180.0f;
    px[i] = x + (int)(cosf(angle) * radius);
    py[i] = y + (int)(sinf(angle) * radius);
  }

  // Draw outer hexagon
  for (int i = 0; i < 6; i++) {
    renderer.drawLine(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], 2, true);
  }

  // Draw inner icosahedron lines representing triangles
  renderer.drawLine(px[0], py[0], px[3], py[3], 1, true); // vertical line
  renderer.drawLine(px[1], py[1], px[4], py[4], 1, true); 
  renderer.drawLine(px[2], py[2], px[5], py[5], 1, true);

  // Draw center shield for number
  drawCircle(renderer, x, y, 22, 1, false);
  fillCircle(renderer, x, y, 21, true);

  std::string txt = std::to_string(value);
  int tW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, txt.c_str());
  int tH = renderer.getTextHeight(NOTOSANS_18_FONT_ID);
  renderer.drawText(NOTOSANS_18_FONT_ID, x - tW / 2, y - tH / 2, txt.c_str(), false, EpdFontFamily::BOLD);
}

void DiceActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Dice & Tools");

  // Draw tabs bar
  const int tabsY = metrics.topPadding + metrics.headerHeight + 20;
  const int tabW = (pageWidth - 40) / 5;
  const std::string tabNames[5] = { "D6", "Coin", "0-10", "Arrow", "D20" };

  for (int i = 0; i < 5; i++) {
    bool active = (currentMode == static_cast<DiceMode>(i));
    int tx = 20 + i * tabW;
    renderer.drawRoundedRect(tx + 2, tabsY, tabW - 4, 30, 1, 5, active);
    renderer.drawText(SMALL_FONT_ID, tx + (tabW - renderer.getTextWidth(SMALL_FONT_ID, tabNames[i].c_str())) / 2, tabsY + 7, tabNames[i].c_str(), !active);
  }

  // Draw main card
  const int cardW = pageWidth - 40;
  const int cardH = pageHeight - tabsY - metrics.buttonHintsHeight - 60;
  const int cardX = 20;
  const int cardY = tabsY + 45;

  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 10, true);

  int cx = cardX + cardW / 2;
  int cy = cardY + cardH / 2 - 10;

  switch (currentMode) {
    case DiceMode::D6:
      drawD6(cx, cy, 120, lastRollD6);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Roll", true, EpdFontFamily::REGULAR);
      break;
    case DiceMode::Coin:
      drawCoin(cx, cy, 140, lastRollCoinHeads);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Flip", true, EpdFontFamily::REGULAR);
      break;
    case DiceMode::Random10: {
      drawCircle(renderer, cx, cy, 60, 1, true);
      fillCircle(renderer, cx, cy, 56, false);
      std::string valStr = std::to_string(lastRollRandom10);
      int vW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, valStr.c_str());
      int vH = renderer.getTextHeight(NOTOSANS_18_FONT_ID);
      renderer.drawText(NOTOSANS_18_FONT_ID, cx - vW / 2, cy - vH / 2, valStr.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Generate (0-10)", true, EpdFontFamily::REGULAR);
      break;
    }
    case DiceMode::Arrow:
      drawArrow(cx, cy, 150, lastRollArrowDirection);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Spin Arrow", true, EpdFontFamily::REGULAR);
      break;
    case DiceMode::D20:
      drawD20(cx, cy, 150, lastRollD20);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Roll D20", true, EpdFontFamily::REGULAR);
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
