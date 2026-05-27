#pragma once

#include "activities/Activity.h"
#include <string>

enum class DiceMode {
  D6,
  Coin,
  Random10,
  Arrow,
  D20
};

class DiceActivity final : public Activity {
 private:
  DiceMode currentMode = DiceMode::D6;

  int lastRollD6 = 1;
  bool lastRollCoinHeads = true;
  int lastRollRandom10 = 0;
  int lastRollArrowDirection = 0; // 0 to 7
  int lastRollD20 = 20;

  void roll();
  void drawD6(int x, int y, int size, int value);
  void drawCoin(int x, int y, int size, bool heads);
  void drawArrow(int x, int y, int size, int direction);
  void drawD20(int x, int y, int size, int value);

 public:
  explicit DiceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dice", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
