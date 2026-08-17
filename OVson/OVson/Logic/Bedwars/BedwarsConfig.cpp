#include "BedwarsConfig.h"

#include "../../Config/Config.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace OVson::Bedwars::Configuration {
namespace {

std::mutex g_mutex;
Settings g_settings;
bool g_initialized = false;

std::unordered_map<std::string, std::string> parsePairs(const std::string &data) {
  std::unordered_map<std::string, std::string> pairs;
  std::size_t start = 0;
  while (start < data.size() && pairs.size() < 512) {
    const std::size_t end = data.find(';', start);
    const std::string pair = data.substr(start, end - start);
    const std::size_t equals = pair.find('=');
    if (equals != std::string::npos && equals > 0)
      pairs.emplace(pair.substr(0, equals), pair.substr(equals + 1));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return pairs;
}

bool readBool(const std::unordered_map<std::string, std::string> &pairs,
              const char *key, bool fallback) {
  const auto it = pairs.find(key);
  if (it == pairs.end())
    return fallback;
  if (it->second == "1" || it->second == "true")
    return true;
  if (it->second == "0" || it->second == "false")
    return false;
  return fallback;
}

float readFloat(const std::unordered_map<std::string, std::string> &pairs,
                const char *key, float fallback, float minimum,
                float maximum) {
  const auto it = pairs.find(key);
  if (it == pairs.end())
    return fallback;
  try {
    std::size_t consumed = 0;
    const float value = std::stof(it->second, &consumed);
    return consumed == it->second.size() && std::isfinite(value)
               ? std::clamp(value, minimum, maximum)
               : fallback;
  } catch (...) {
    return fallback;
  }
}

int readInt(const std::unordered_map<std::string, std::string> &pairs,
            const char *key, int fallback, int minimum, int maximum) {
  const auto it = pairs.find(key);
  if (it == pairs.end())
    return fallback;
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(it->second, &consumed);
    return consumed == it->second.size()
               ? std::clamp(value, minimum, maximum)
               : fallback;
  } catch (...) {
    return fallback;
  }
}

std::unordered_map<std::string, int>
readMapOverrides(const std::unordered_map<std::string, std::string> &pairs) {
  std::unordered_map<std::string, int> result;
  const auto it = pairs.find("mapOverrides");
  if (it == pairs.end())
    return result;
  std::size_t start = 0;
  while (start < it->second.size() && result.size() < 64) {
    const std::size_t end = it->second.find('|', start);
    const std::string entry = it->second.substr(start, end - start);
    const std::size_t colon = entry.rfind(':');
    if (colon != std::string::npos) {
      const std::string map = normalizeMapName(entry.substr(0, colon));
      try {
        std::size_t consumed = 0;
        const int value = std::stoi(entry.substr(colon + 1), &consumed);
        if (!map.empty() && consumed == entry.size() - colon - 1 &&
            value >= 1 && value <= 511)
          result[map] = value;
      } catch (...) {
      }
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return result;
}

void ensureInitializedLocked() {
  if (g_initialized)
    return;
  g_settings = deserialize(Config::getBedwarsSettingsData());
  g_initialized = true;
}

template <typename Fn> void mutate(Fn &&fn) {
  Settings copy;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ensureInitializedLocked();
    fn(g_settings);
    g_settings = deserialize(serialize(g_settings));
    copy = g_settings;
  }
  Config::setBedwarsSettingsData(serialize(copy));
}

} // namespace

Settings::Settings() {
  hud[static_cast<std::size_t>(HudId::EventTimer)] = {false, 0.02F, 0.18F, 1.0F};
  hud[static_cast<std::size_t>(HudId::Height)] = {false, 0.02F, 0.34F, 1.0F};
  hud[static_cast<std::size_t>(HudId::Resource)] = {false, 0.78F, 0.44F, 1.0F};
  hud[static_cast<std::size_t>(HudId::TeamState)] = {false, 0.78F, 0.18F, 1.0F};
  hud[static_cast<std::size_t>(HudId::BedDistance)] = {false, 0.02F, 0.62F, 1.0F};
  hud[static_cast<std::size_t>(HudId::BedStatus)] = {false, 0.78F, 0.64F, 1.0F};
}

bool Settings::enabled(Module module) const {
  const auto index = static_cast<std::size_t>(module);
  return masterEnabled && index < modules.size() && modules[index];
}

Settings get() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureInitializedLocked();
  return g_settings;
}

void initialize() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureInitializedLocked();
}

void reload() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_settings = deserialize(Config::getBedwarsSettingsData());
  g_initialized = true;
}

void save(const Settings &settings) {
  const Settings sanitized = deserialize(serialize(settings));
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings = sanitized;
    g_initialized = true;
  }
  Config::setBedwarsSettingsData(serialize(sanitized));
}

bool isMasterEnabled() { return get().masterEnabled; }
void setMasterEnabled(bool enabled) {
  mutate([&](Settings &settings) { settings.masterEnabled = enabled; });
}
bool isModuleEnabled(Module module) {
  const Settings settings = get();
  const auto index = static_cast<std::size_t>(module);
  return index < settings.modules.size() && settings.modules[index];
}
void setModuleEnabled(Module module, bool enabled) {
  if (!isModuleAvailable(module))
    return;
  mutate([&](Settings &settings) {
    const auto index = static_cast<std::size_t>(module);
    if (index < settings.modules.size())
      settings.modules[index] = enabled;
  });
}
bool isDebugEnabled() { return get().debug; }
void setDebugEnabled(bool enabled) {
  mutate([&](Settings &settings) { settings.debug = enabled; });
}
bool areSoundsEnabled() { return get().sounds; }
void setSoundsEnabled(bool enabled) {
  mutate([&](Settings &settings) { settings.sounds = enabled; });
}
bool isOnlyNextEvent() { return get().onlyNextEvent; }
void setOnlyNextEvent(bool enabled) {
  mutate([&](Settings &settings) { settings.onlyNextEvent = enabled; });
}
bool isDynamicTimerColor() { return get().dynamicTimerColor; }
void setDynamicTimerColor(bool enabled) {
  mutate([&](Settings &settings) { settings.dynamicTimerColor = enabled; });
}
bool isDynamicHeightColor() { return get().dynamicHeightColor; }
void setDynamicHeightColor(bool enabled) {
  mutate([&](Settings &settings) { settings.dynamicHeightColor = enabled; });
}
bool isShortUpgradeLabels() { return get().shortUpgradeLabels; }
void setShortUpgradeLabels(bool enabled) {
  mutate([&](Settings &settings) { settings.shortUpgradeLabels = enabled; });
}
bool isResourceHudEnabled() {
  return get().hud[static_cast<std::size_t>(HudId::Resource)].visible;
}
void setResourceHudEnabled(bool enabled) {
  mutate([&](Settings &settings) {
    settings.resourceHud = enabled;
    settings.hud[static_cast<std::size_t>(HudId::Resource)].visible = enabled;
  });
}
bool isStackedResourceAlerts() { return get().stackedResourceAlerts; }
void setStackedResourceAlerts(bool enabled) {
  mutate([&](Settings &settings) {
    settings.stackedResourceAlerts = enabled;
  });
}
bool isResourceEnabled(Resource resource) {
  const auto settings = get();
  const auto index = static_cast<std::size_t>(resource);
  return index < settings.resources.size() && settings.resources[index];
}
void setResourceEnabled(Resource resource, bool enabled) {
  mutate([&](Settings &settings) {
    const auto index = static_cast<std::size_t>(resource);
    if (index < settings.resources.size())
      settings.resources[index] = enabled;
  });
}
float getTimerX() {
  return get().hud[static_cast<std::size_t>(HudId::EventTimer)].x;
}
void setTimerX(float value) {
  mutate([&](Settings &settings) {
    settings.timerX = std::clamp(value, 0.0F, 1.0F);
    settings.hud[static_cast<std::size_t>(HudId::EventTimer)].x = settings.timerX;
  });
}
float getTimerY() {
  return get().hud[static_cast<std::size_t>(HudId::EventTimer)].y;
}
void setTimerY(float value) {
  mutate([&](Settings &settings) {
    settings.timerY = std::clamp(value, 0.0F, 1.0F);
    settings.hud[static_cast<std::size_t>(HudId::EventTimer)].y = settings.timerY;
  });
}
float getTimerScale() {
  return get().hud[static_cast<std::size_t>(HudId::EventTimer)].scale;
}
void setTimerScale(float value) {
  mutate([&](Settings &settings) {
    settings.timerScale = std::clamp(value, 0.5F, 2.5F);
    settings.hud[static_cast<std::size_t>(HudId::EventTimer)].scale =
        settings.timerScale;
  });
}
float getHeightX() {
  return get().hud[static_cast<std::size_t>(HudId::Height)].x;
}
void setHeightX(float value) {
  mutate([&](Settings &settings) {
    settings.heightX = std::clamp(value, 0.0F, 1.0F);
    settings.hud[static_cast<std::size_t>(HudId::Height)].x = settings.heightX;
  });
}
float getHeightY() {
  return get().hud[static_cast<std::size_t>(HudId::Height)].y;
}
void setHeightY(float value) {
  mutate([&](Settings &settings) {
    settings.heightY = std::clamp(value, 0.0F, 1.0F);
    settings.hud[static_cast<std::size_t>(HudId::Height)].y = settings.heightY;
  });
}
float getHeightScale() {
  return get().hud[static_cast<std::size_t>(HudId::Height)].scale;
}
void setHeightScale(float value) {
  mutate([&](Settings &settings) {
    settings.heightScale = std::clamp(value, 0.5F, 2.5F);
    settings.hud[static_cast<std::size_t>(HudId::Height)].scale =
        settings.heightScale;
  });
}
float getPlayerAlertRange() { return get().playerAlertRange; }
void setPlayerAlertRange(float range) {
  mutate([&](Settings &settings) {
    settings.playerAlertRange = std::clamp(range, 4.0F, 256.0F);
  });
}
float getBedWarningRange() { return get().bedWarningRange; }
void setBedWarningRange(float range) {
  mutate([&](Settings &settings) {
    settings.bedWarningRange = std::clamp(range, 5.0F, 128.0F);
  });
}
float getBedMaximumRange() { return get().bedMaximumRange; }
void setBedMaximumRange(float range) {
  mutate([&](Settings &settings) {
    settings.bedMaximumRange = std::clamp(range, 16.0F, 160.0F);
  });
}
int getBedScanIntervalMs() { return get().bedScanIntervalMs; }
void setBedScanIntervalMs(int interval) {
  mutate([&](Settings &settings) {
    settings.bedScanIntervalMs = std::clamp(interval, 5000, 30000);
  });
}
int getHeightLimitOverride() { return get().heightLimitOverride; }
void setHeightLimitOverride(int limit) {
  mutate([&](Settings &settings) {
    settings.heightLimitOverride = std::clamp(limit, 0, 512);
  });
}
int getPlayerAlertCooldownMs() { return get().playerAlertCooldownMs; }
void setPlayerAlertCooldownMs(int cooldown) {
  mutate([&](Settings &settings) {
    settings.playerAlertCooldownMs = std::clamp(cooldown, 250, 60000);
  });
}
int getTrapReminderSeconds() { return get().trapReminderSeconds; }
void setTrapReminderSeconds(int seconds) {
  mutate([&](Settings &settings) {
    settings.trapReminderSeconds = std::clamp(seconds, 15, 600);
  });
}

VisibilityMode getVisibilityMode() { return get().visibilityMode; }
void setVisibilityMode(VisibilityMode mode) {
  mutate([&](Settings &settings) {
    const auto value = static_cast<std::size_t>(mode);
    settings.visibilityMode =
        value <= static_cast<std::size_t>(VisibilityMode::CameraView)
            ? mode
            : VisibilityMode::LineOfSight;
  });
}
float getCameraViewDegrees() { return get().cameraViewDegrees; }
void setCameraViewDegrees(float degrees) {
  mutate([&](Settings &settings) {
    settings.cameraViewDegrees = std::clamp(degrees, 30.0F, 170.0F);
  });
}
HudLayout getHudLayout(HudId hud) {
  const auto settings = get();
  const auto index = static_cast<std::size_t>(hud);
  return index < settings.hud.size() ? settings.hud[index] : HudLayout{};
}
void setHudLayout(HudId hud, const HudLayout &layout) {
  mutate([&](Settings &settings) {
    const auto index = static_cast<std::size_t>(hud);
    if (index < settings.hud.size())
      settings.hud[index] = sanitizeHudLayout(layout, 0.04F, 0.04F);
  });
}
void resetHudLayout(HudId hud) {
  const Settings defaults;
  const auto index = static_cast<std::size_t>(hud);
  if (index < defaults.hud.size())
    setHudLayout(hud, defaults.hud[index]);
}
void resetAllHudLayouts() {
  const Settings defaults;
  mutate([&](Settings &settings) { settings.hud = defaults.hud; });
}
float getDefaultNotificationSeconds() { return get().defaultNotificationSeconds; }
void setDefaultNotificationSeconds(float seconds) {
  mutate([&](Settings &settings) {
    settings.defaultNotificationSeconds = std::clamp(seconds, 1.0F, 15.0F);
  });
}
float getImportantNotificationSeconds() { return get().importantNotificationSeconds; }
void setImportantNotificationSeconds(float seconds) {
  mutate([&](Settings &settings) {
    settings.importantNotificationSeconds = std::clamp(seconds, 1.0F, 15.0F);
  });
}
float getPlayerNotificationSeconds() { return get().playerNotificationSeconds; }
void setPlayerNotificationSeconds(float seconds) {
  mutate([&](Settings &settings) {
    settings.playerNotificationSeconds = std::clamp(seconds, 1.0F, 15.0F);
  });
}
float getWarningNotificationSeconds() { return get().warningNotificationSeconds; }
void setWarningNotificationSeconds(float seconds) {
  mutate([&](Settings &settings) {
    settings.warningNotificationSeconds = std::clamp(seconds, 1.0F, 15.0F);
  });
}
int getMaximumVisibleNotifications() { return get().maximumVisibleNotifications; }
void setMaximumVisibleNotifications(int maximum) {
  mutate([&](Settings &settings) {
    settings.maximumVisibleNotifications = std::clamp(maximum, 1, 10);
  });
}
void setMapPlacementOverride(const std::string &mapName, int placementY) {
  const std::string normalized = normalizeMapName(mapName);
  if (normalized.empty())
    return;
  mutate([&](Settings &settings) {
    if (settings.mapPlacementOverrides.size() >= 64 &&
        settings.mapPlacementOverrides.find(normalized) ==
            settings.mapPlacementOverrides.end())
      return;
    settings.mapPlacementOverrides[normalized] = std::clamp(placementY, 1, 511);
  });
}
void resetMapPlacementOverride(const std::string &mapName) {
  const std::string normalized = normalizeMapName(mapName);
  mutate([&](Settings &settings) { settings.mapPlacementOverrides.erase(normalized); });
}

std::string serialize(const Settings &settings) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << "version=3;master=" << settings.masterEnabled << ';';
  for (std::size_t i = 0; i < kModuleCount; ++i)
    out << moduleKey(static_cast<Module>(i)) << '=' << settings.modules[i] << ';';
  out << "debug=" << settings.debug << ";sounds=" << settings.sounds
      << ";onlyNextEvent=" << settings.onlyNextEvent
      << ";dynamicTimerColor=" << settings.dynamicTimerColor
      << ";dynamicHeightColor=" << settings.dynamicHeightColor
      << ";shortUpgradeLabels=" << settings.shortUpgradeLabels
      << ";resourceHud=" << settings.resourceHud
      << ";stackedResourceAlerts=" << settings.stackedResourceAlerts
      << ";resourceIron=" << settings.resources[0]
      << ";resourceGold=" << settings.resources[1]
      << ";resourceDiamond=" << settings.resources[2]
      << ";resourceEmerald=" << settings.resources[3]
      << ";shopDuplicatePrevention=" << settings.shopDuplicatePrevention
      << ";timerX=" << settings.timerX << ";timerY=" << settings.timerY
      << ";timerScale=" << settings.timerScale
      << ";heightX=" << settings.heightX << ";heightY=" << settings.heightY
      << ";heightScale=" << settings.heightScale
      << ";heightLimitOverride=" << settings.heightLimitOverride
      << ";bedWarningRange=" << settings.bedWarningRange
      << ";bedMaximumRange=" << settings.bedMaximumRange
      << ";bedScanIntervalMs=" << settings.bedScanIntervalMs
      << ";playerAlertRange=" << settings.playerAlertRange
      << ";playerAlertCooldownMs=" << settings.playerAlertCooldownMs
      << ";trapReminderSeconds=" << settings.trapReminderSeconds
      << ";visibilityMode=" << static_cast<int>(settings.visibilityMode)
      << ";cameraViewDegrees=" << settings.cameraViewDegrees
      << ";notificationDefault=" << settings.defaultNotificationSeconds
      << ";notificationImportant=" << settings.importantNotificationSeconds
      << ";notificationPlayer=" << settings.playerNotificationSeconds
      << ";notificationWarning=" << settings.warningNotificationSeconds
      << ";notificationMaximum=" << settings.maximumVisibleNotifications
      << ';';
  for (std::size_t i = 0; i < kHudCount; ++i) {
    const auto &hud = settings.hud[i];
    out << "hud" << i << "Visible=" << hud.visible << ";hud" << i
        << "X=" << hud.x << ";hud" << i << "Y=" << hud.y << ";hud" << i
        << "Scale=" << hud.scale << ';';
  }
  std::vector<std::pair<std::string, int>> overrides(
      settings.mapPlacementOverrides.begin(),
      settings.mapPlacementOverrides.end());
  std::sort(overrides.begin(), overrides.end());
  out << "mapOverrides=";
  for (std::size_t i = 0; i < overrides.size() && i < 64; ++i) {
    if (i != 0)
      out << '|';
    out << normalizeMapName(overrides[i].first) << ':'
        << std::clamp(overrides[i].second, 1, 511);
  }
  out << ';';
  return out.str();
}

Settings deserialize(const std::string &data) {
  Settings settings;
  const auto pairs = parsePairs(data);
  // An empty file is a fresh v3 configuration.  Only persisted legacy data
  // without a version marker should receive the v1 compatibility defaults.
  const int version = data.empty() ? 3 : readInt(pairs, "version", 1, 1, 3);
  settings.formatVersion = 3;
  settings.masterEnabled = readBool(pairs, "master", false);
  for (std::size_t i = 0; i < kModuleCount; ++i)
    settings.modules[i] =
        isModuleAvailable(static_cast<Module>(i))
            ? readBool(pairs, moduleKey(static_cast<Module>(i)), false)
            : false;
  settings.debug = readBool(pairs, "debug", false);
  settings.sounds = readBool(pairs, "sounds", true);
  settings.onlyNextEvent = readBool(pairs, "onlyNextEvent", true);
  settings.dynamicTimerColor = readBool(pairs, "dynamicTimerColor", true);
  settings.dynamicHeightColor = readBool(pairs, "dynamicHeightColor", true);
  settings.shortUpgradeLabels = readBool(pairs, "shortUpgradeLabels", true);
  settings.resourceHud = readBool(pairs, "resourceHud", false);
  settings.stackedResourceAlerts =
      readBool(pairs, "stackedResourceAlerts", true);
  settings.resources[0] = readBool(pairs, "resourceIron", true);
  settings.resources[1] = readBool(pairs, "resourceGold", true);
  settings.resources[2] = readBool(pairs, "resourceDiamond", true);
  settings.resources[3] = readBool(pairs, "resourceEmerald", true);
  settings.shopDuplicatePrevention =
      readBool(pairs, "shopDuplicatePrevention", false);
  settings.timerX = readFloat(pairs, "timerX", 0.02F, 0.0F, 1.0F);
  settings.timerY = readFloat(pairs, "timerY", 0.20F, 0.0F, 1.0F);
  settings.timerScale = readFloat(pairs, "timerScale", 1.0F, 0.5F, 2.5F);
  settings.heightX = readFloat(pairs, "heightX", 0.02F, 0.0F, 1.0F);
  settings.heightY = readFloat(pairs, "heightY", 0.45F, 0.0F, 1.0F);
  settings.heightScale = readFloat(pairs, "heightScale", 1.0F, 0.5F, 2.5F);
  settings.heightLimitOverride =
      readInt(pairs, "heightLimitOverride", 0, 0, 512);
  settings.bedWarningRange =
      readFloat(pairs, "bedWarningRange", 40.0F, 5.0F, 128.0F);
  settings.bedMaximumRange =
      readFloat(pairs, "bedMaximumRange", 96.0F, 16.0F, 160.0F);
  settings.bedScanIntervalMs =
      readInt(pairs, "bedScanIntervalMs", 10000, 5000, 30000);
  settings.playerAlertRange =
      readFloat(pairs, "playerAlertRange", 32.0F, 4.0F, 256.0F);
  settings.playerAlertCooldownMs =
      readInt(pairs, "playerAlertCooldownMs", 2500, 250, 60000);
  settings.trapReminderSeconds =
      readInt(pairs, "trapReminderSeconds", 90, 15, 600);
  settings.visibilityMode = static_cast<VisibilityMode>(readInt(
      pairs, "visibilityMode",
      version == 1 ? static_cast<int>(VisibilityMode::RangeOnly)
                   : static_cast<int>(VisibilityMode::LineOfSight),
      static_cast<int>(VisibilityMode::RangeOnly),
      static_cast<int>(VisibilityMode::CameraView)));
  settings.cameraViewDegrees =
      readFloat(pairs, "cameraViewDegrees", 100.0F, 30.0F, 170.0F);
  settings.defaultNotificationSeconds =
      readFloat(pairs, "notificationDefault", 3.0F, 1.0F, 15.0F);
  settings.importantNotificationSeconds =
      readFloat(pairs, "notificationImportant", 5.0F, 1.0F, 15.0F);
  settings.playerNotificationSeconds =
      readFloat(pairs, "notificationPlayer", 4.0F, 1.0F, 15.0F);
  settings.warningNotificationSeconds =
      readFloat(pairs, "notificationWarning", 5.0F, 1.0F, 15.0F);
  settings.maximumVisibleNotifications =
      readInt(pairs, "notificationMaximum", 5, 1, 10);
  for (std::size_t i = 0; i < kHudCount; ++i) {
    const std::string prefix = "hud" + std::to_string(i);
    settings.hud[i].visible =
        readBool(pairs, (prefix + "Visible").c_str(), settings.hud[i].visible);
    settings.hud[i].x =
        readFloat(pairs, (prefix + "X").c_str(), settings.hud[i].x, 0.0F, 1.0F);
    settings.hud[i].y =
        readFloat(pairs, (prefix + "Y").c_str(), settings.hud[i].y, 0.0F, 1.0F);
    settings.hud[i].scale = readFloat(pairs, (prefix + "Scale").c_str(),
                                      settings.hud[i].scale, 0.5F, 2.5F);
  }
  if (version == 1) {
    settings.hud[static_cast<std::size_t>(HudId::EventTimer)] = {
        settings.modules[static_cast<std::size_t>(Module::EventTimers)],
        settings.timerX, settings.timerY, settings.timerScale};
    settings.hud[static_cast<std::size_t>(HudId::Height)] = {
        settings.modules[static_cast<std::size_t>(Module::HeightOverlay)],
        settings.heightX, settings.heightY, settings.heightScale};
    settings.hud[static_cast<std::size_t>(HudId::Resource)].visible =
        settings.resourceHud &&
        settings.modules[static_cast<std::size_t>(Module::ResourceTracker)];
    settings.hud[static_cast<std::size_t>(HudId::TeamState)].visible =
        settings.modules[static_cast<std::size_t>(Module::UpgradeHud)];
    settings.hud[static_cast<std::size_t>(HudId::BedDistance)].visible =
        settings.modules[static_cast<std::size_t>(Module::BedTracker)];
  }
  settings.resourceHud =
      settings.hud[static_cast<std::size_t>(HudId::Resource)].visible;
  settings.timerX = settings.hud[static_cast<std::size_t>(HudId::EventTimer)].x;
  settings.timerY = settings.hud[static_cast<std::size_t>(HudId::EventTimer)].y;
  settings.timerScale =
      settings.hud[static_cast<std::size_t>(HudId::EventTimer)].scale;
  settings.heightX = settings.hud[static_cast<std::size_t>(HudId::Height)].x;
  settings.heightY = settings.hud[static_cast<std::size_t>(HudId::Height)].y;
  settings.heightScale = settings.hud[static_cast<std::size_t>(HudId::Height)].scale;
  settings.mapPlacementOverrides = readMapOverrides(pairs);
  return settings;
}

} // namespace OVson::Bedwars::Configuration
