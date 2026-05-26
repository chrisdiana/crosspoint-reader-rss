#pragma once

#include "../App.h"

class CalculatorApp final : public App {
 public:
  std::string getName() const override { return "Calculator"; }
  UIIcon getIcon() const override { return UIIcon::Calculator; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
