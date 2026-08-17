#pragma once

#include "BedwarsCore.h"

#include <array>
#include <string>
#include <unordered_map>

namespace OVson::Bedwars::Configuration {

struct Settings {
  Settings();
  int formatVersion = 3;
  bool masterEnabled = false;
  std::array<bool, kModuleCount> modules{};
  bool debug = false;
  bool sounds = true;
  bool onlyNextEvent = true;
  bool dynamicTimerColor = true;
  bool dynamicHeightColor = true;
  bool shortUpgradeLabels = true;
  bool resourceHud = false;
  bool stackedResourceAlerts = true;
  std::array<bool, kResourceCount> resources = {true, true, true, true};
  bool shopDuplicatePrevention = false;
  float timerX = 0.02F;
  float timerY = 0.20F;
  float timerScale = 1.0F;
  float heightX = 0.02F;
  float heightY = 0.45F;
  float heightScale = 1.0F;
  int heightLimitOverride = 0;
  float bedWarningRange = 40.0F;
  float bedMaximumRange = 96.0F;
  int bedScanIntervalMs = 10000;
  float playerAlertRange = 32.0F;
  int playerAlertCooldownMs = 2500;
  int trapReminderSeconds = 90;
  VisibilityMode visibilityMode = VisibilityMode::LineOfSight;
  float cameraViewDegrees = 100.0F;
  float defaultNotificationSeconds = 3.0F;
  float importantNotificationSeconds = 5.0F;
  float playerNotificationSeconds = 4.0F;
  float warningNotificationSeconds = 5.0F;
  int maximumVisibleNotifications = 5;
  std::array<HudLayout, kHudCount> hud{};
  std::unordered_map<std::string, int> mapPlacementOverrides;

  bool enabled(Module module) const;
};

Settings get();
void initialize();
void reload();
void save(const Settings &settings);

bool isMasterEnabled();
void setMasterEnabled(bool enabled);
bool isModuleEnabled(Module module);
void setModuleEnabled(Module module, bool enabled);
bool isDebugEnabled();
void setDebugEnabled(bool enabled);
bool areSoundsEnabled();
void setSoundsEnabled(bool enabled);
bool isOnlyNextEvent();
void setOnlyNextEvent(bool enabled);
bool isDynamicTimerColor();
void setDynamicTimerColor(bool enabled);
bool isDynamicHeightColor();
void setDynamicHeightColor(bool enabled);
bool isShortUpgradeLabels();
void setShortUpgradeLabels(bool enabled);
bool isResourceHudEnabled();
void setResourceHudEnabled(bool enabled);
bool isStackedResourceAlerts();
void setStackedResourceAlerts(bool enabled);
bool isResourceEnabled(Resource resource);
void setResourceEnabled(Resource resource, bool enabled);
float getTimerX();
void setTimerX(float value);
float getTimerY();
void setTimerY(float value);
float getTimerScale();
void setTimerScale(float value);
float getHeightX();
void setHeightX(float value);
float getHeightY();
void setHeightY(float value);
float getHeightScale();
void setHeightScale(float value);
float getPlayerAlertRange();
void setPlayerAlertRange(float range);
float getBedWarningRange();
void setBedWarningRange(float range);
float getBedMaximumRange();
void setBedMaximumRange(float range);
int getBedScanIntervalMs();
void setBedScanIntervalMs(int interval);
int getHeightLimitOverride();
void setHeightLimitOverride(int limit);
int getPlayerAlertCooldownMs();
void setPlayerAlertCooldownMs(int cooldown);
int getTrapReminderSeconds();
void setTrapReminderSeconds(int seconds);
VisibilityMode getVisibilityMode();
void setVisibilityMode(VisibilityMode mode);
float getCameraViewDegrees();
void setCameraViewDegrees(float degrees);
HudLayout getHudLayout(HudId hud);
void setHudLayout(HudId hud, const HudLayout &layout);
void resetHudLayout(HudId hud);
void resetAllHudLayouts();
float getDefaultNotificationSeconds();
void setDefaultNotificationSeconds(float seconds);
float getImportantNotificationSeconds();
void setImportantNotificationSeconds(float seconds);
float getPlayerNotificationSeconds();
void setPlayerNotificationSeconds(float seconds);
float getWarningNotificationSeconds();
void setWarningNotificationSeconds(float seconds);
int getMaximumVisibleNotifications();
void setMaximumVisibleNotifications(int maximum);
void setMapPlacementOverride(const std::string &mapName, int placementY);
void resetMapPlacementOverride(const std::string &mapName);

std::string serialize(const Settings &settings);
Settings deserialize(const std::string &data);

} // namespace OVson::Bedwars::Configuration
