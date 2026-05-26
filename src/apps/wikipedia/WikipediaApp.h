#pragma once

#include "../App.h"

class WikipediaApp final : public App {
 public:
  std::string getName() const override { return "Wikipedia"; }
  UIIcon getIcon() const override { return UIIcon::Wikipedia; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
