#pragma once

#include "BedwarsCore.h"

#include <array>
#include <string>
#include <unordered_map>

namespace OVson::Bedwars::Configuration {

// Values that used to be user-tunable. They are now fixed: the defaults were
// what nearly everyone ran, and each extra slider cost more in menu clutter
// than it ever returned in usefulness.
namespace Fixed {
inline constexpr bool kDynamicTimerColor = true;
inline constexpr bool kDynamicHeightColor = true;
inline constexpr bool kShortUpgradeLabels = true;
inline constexpr bool kStackedResourceAlerts = true;
inline constexpr int kPlayerAlertCooldownMs = 2500;
inline constexpr float kCameraViewDegrees = 100.0F;
inline constexpr float kDefaultNotificationSeconds = 3.0F;
inline constexpr float kImportantNotificationSeconds = 5.0F;
inline constexpr float kPlayerNotificationSeconds = 4.0F;
inline constexpr float kWarningNotificationSeconds = 5.0F;
inline constexpr int kMaximumVisibleNotifications = 5;
} // namespace Fixed

struct Settings {
  Settings();
  int formatVersion = 4;
  bool masterEnabled = false;
  std::array<bool, kModuleCount> modules{};
  bool debug = false;
  bool sounds = true;
  bool onlyNextEvent = true;
  bool resourceHud = false;
  std::array<bool, kResourceCount> resources = {true, true, true, true};
  bool shopDuplicatePrevention = false;
  float timerX = 0.02F;
  float timerY = 0.20F;
  float timerScale = 1.0F;
  float heightX = 0.02F;
  float heightY = 0.45F;
  float heightScale = 1.0F;
  int heightLimitOverride = 0;
  float playerAlertRange = 32.0F;
  int trapReminderSeconds = 90;
  VisibilityMode visibilityMode = VisibilityMode::LineOfSight;
  AlertOutput alertOutput = AlertOutput::Overlay;
  std::array<HudLayout, kHudCount> hud{};

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
bool isResourceHudEnabled();
void setResourceHudEnabled(bool enabled);
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
int getHeightLimitOverride();
void setHeightLimitOverride(int limit);
int getTrapReminderSeconds();
void setTrapReminderSeconds(int seconds);
VisibilityMode getVisibilityMode();
void setVisibilityMode(VisibilityMode mode);
AlertOutput getAlertOutput();
void setAlertOutput(AlertOutput output);
HudLayout getHudLayout(HudId hud);
void setHudLayout(HudId hud, const HudLayout &layout);
void resetHudLayout(HudId hud);
void resetAllHudLayouts();

std::string serialize(const Settings &settings);
Settings deserialize(const std::string &data);

} // namespace OVson::Bedwars::Configuration
