#pragma once

#include <functional>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  bool prepareRenderableBmp(int pageWidth, int pageHeight, std::string& renderPath);
  void doSetSleepCover();

  std::string filePath;
  std::string renderBmpPath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
