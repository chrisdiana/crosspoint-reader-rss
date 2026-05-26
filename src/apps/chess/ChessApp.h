#pragma once

#include "../App.h"

class ChessApp final : public App {
 public:
  std::string getName() const override { return "Chess"; }
  UIIcon getIcon() const override { return UIIcon::Chess; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
