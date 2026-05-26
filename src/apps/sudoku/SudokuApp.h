#pragma once

#include "apps/App.h"

class SudokuApp final : public App {
 public:
  std::string getName() const override { return "Sudoku"; }
  UIIcon getIcon() const override { return UIIcon::Sudoku; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
