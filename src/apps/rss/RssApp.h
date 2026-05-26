#pragma once

#include "../App.h"

class RssApp final : public App {
 public:
  std::string getName() const override { return "RSS Feeds"; }
  UIIcon getIcon() const override { return UIIcon::Rss; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
