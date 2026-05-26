#pragma once

#include "apps/App.h"

class WeatherApp final : public App {
 public:
  std::string getName() const override { return "Weather"; }
  UIIcon getIcon() const override { return UIIcon::Weather; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
