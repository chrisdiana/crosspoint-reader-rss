#pragma once

#include "apps/App.h"

class RedditApp final : public App {
 public:
  std::string getName() const override { return "Reddit"; }
  UIIcon getIcon() const override { return UIIcon::Reddit; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
