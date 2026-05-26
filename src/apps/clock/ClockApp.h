#pragma once

#include "../App.h"

class ClockApp final : public App {
 public:
  std::string getName() const override { return "Clock"; }
  UIIcon getIcon() const override { return UIIcon::Clock; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
