#pragma once

#include "../App.h"

class DiceApp final : public App {
 public:
  std::string getName() const override { return "Dice"; }
  UIIcon getIcon() const override { return UIIcon::Dice; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
