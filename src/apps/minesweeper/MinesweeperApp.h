#pragma once

#include "../App.h"

class MinesweeperApp final : public App {
 public:
  std::string getName() const override { return "Minesweeper"; }
  UIIcon getIcon() const override { return UIIcon::Minesweeper; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
