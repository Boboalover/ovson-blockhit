#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Tabs.h"

#include "../Helpers.h"
#include "../State.h"
#include "../Theme.h"
#include "../../Logic/Bedwars/BedwarsConfig.h"
#include "../../Logic/Bedwars/BedwarsCore.h"
#include "../../Logic/Bedwars/BedwarsRuntime.h"
#include "../../Render/BedwarsOverlay.h"

#include <gl/GL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace Render::Tabs {

void renderBedwars(TabCtx &ctx) {
  using namespace ClickGUIState;
  using namespace OVson::Bedwars;
  namespace BwConfig = OVson::Bedwars::Configuration;

  const float panelX = ctx.mainX + 190.0F;
  const float panelW = std::max(300.0F, g_w - 210.0F);
  const float switchX = panelX + panelW - 45.0F;
  float &cy = ctx.cy;
  const auto settings = BwConfig::get();
  const auto runtime = Runtime::instance().snapshot();
  const bool master = settings.masterEnabled;

  static std::array<bool, 9> expanded = {true, true, false, false, false,
                                         false, false, false, false};
  static ULONGLONG resetArmedAt = 0;

  auto section = [&](std::size_t id, const char *title) {
    const float y = cy - 6.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, panelX, y, panelW, 32.0F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 32.0F, hover, ctx.alpha,
                  expanded[id]);
    glEnable(GL_TEXTURE_2D);
    const std::string label = std::string(expanded[id] ? "-  " : "+  ") + title;
    g_guiFont.drawString(ctx.cx, cy, label,
                         applyAlpha(0xFFE8F3FF, ctx.alpha), 0.48F);
    if (ctx.clickEvent && hover)
      expanded[id] = !expanded[id];
    cy += 39.0F;
    return expanded[id];
  };

  auto toggleRow = [&](const char *title, const char *description, bool value,
                       int switchId, bool available, bool child, auto setter) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, panelX, y, panelW, 46.0F);
    const bool active = available && (!child || master);
    const float alpha = ctx.alpha * (active ? 1.0F : 0.48F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 46.0F, hover, ctx.alpha, value && active);
    drawSwitch(switchId, switchX, cy + 4.0F, value, hover && available, alpha);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(ctx.cx, cy, title, applyAlpha(0xFFFFFFFF, alpha),
                         0.46F);
    std::string detail = description;
    if (child && !master)
      detail += " (saved preference; master is off)";
    else if (!available)
      detail += " (unavailable this session)";
    g_guiFont.drawString(ctx.cx, cy + 17.0F, detail,
                         applyAlpha(0xFFA0A0A5, alpha), 0.36F);
    if (ctx.clickEvent && hover && available)
      setter(!value);
    cy += 52.0F;
  };

  auto sliderRow = [&](const char *title, float value, float minimum,
                       float maximum, int sliderId, const char *suffix,
                       bool child, auto setter) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, panelX, y, panelW, 42.0F);
    const float alpha = ctx.alpha * ((!child || master) ? 1.0F : 0.48F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 42.0F, hover, ctx.alpha);
    float changedValue = value;
    const bool changed = drawSlider(
        sliderId, ctx.cx + 145.0F, cy + 10.0F,
        std::max(80.0F, panelW - 245.0F), 6.0F, changedValue, minimum,
        maximum, ctx.mx, ctx.my, ctx.lClick, ctx.alpha);
    glEnable(GL_TEXTURE_2D);
    char number[40]{};
    std::snprintf(number, sizeof(number), "%.0f%s", value, suffix);
    g_guiFont.drawString(ctx.cx, cy, title, applyAlpha(0xFFFFFFFF, alpha),
                         0.42F);
    g_guiFont.drawString(panelX + panelW - 62.0F, cy, number,
                         applyAlpha(0xFFA0A0A5, alpha), 0.38F);
    if (changed)
      setter(changedValue);
    cy += 48.0F;
  };

  auto actionRow = [&](const std::string &title, const std::string &description,
                       bool available, auto action) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, panelX, y, panelW, 42.0F);
    const float alpha = ctx.alpha * (available ? 1.0F : 0.45F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 42.0F, hover && available, ctx.alpha);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(ctx.cx, cy, title,
                         applyAlpha(0xFFE7F3FF, alpha), 0.43F);
    g_guiFont.drawString(ctx.cx, cy + 16.0F, description,
                         applyAlpha(0xFFA0A0A5, alpha), 0.35F);
    if (ctx.clickEvent && hover && available)
      action();
    cy += 48.0F;
  };

  auto moduleRow = [&](Module module, const char *description) {
    bool available = isModuleAvailable(module);
    if (module == Module::AntiMisplace)
      available = available && Runtime::instance().inputHookAvailable();
    toggleRow(moduleName(module), description,
              BwConfig::isModuleEnabled(module),
              540 + static_cast<int>(module), available, true,
              [module](bool enabled) {
                BwConfig::setModuleEnabled(module, enabled);
              });
  };

  drawSectionLabel(ctx.cx, cy, "Bedwars Tools", ctx.alpha);
  cy += 31.0F;
  g_guiFont.drawString(
      ctx.cx, cy,
      std::string("Status: ") +
          (runtime.lifecycleStatus.empty()
               ? (master ? "Waiting for game state" : "Disabled")
               : runtime.lifecycleStatus),
      applyAlpha(master ? 0xFF8FD6FF : 0xFF9A9AA0, ctx.alpha), 0.44F);
  cy += 27.0F;

  if (section(0, "Overview")) {
    toggleRow("Bedwars Tools", "Master runtime gate; saved child choices remain intact",
              master, 500, true, false,
              [](bool enabled) { BwConfig::setMasterEnabled(enabled); });
    toggleRow("Alert Sounds", "Built-in game feedback for Bedwars alerts",
              settings.sounds, 501, true, true,
              [](bool enabled) { BwConfig::setSoundsEnabled(enabled); });
    actionRow("Reset Bedwars Settings",
              resetArmedAt != 0 && GetTickCount64() - resetArmedAt < 8000
                  ? "Click again within 8 seconds to confirm"
                  : "Two-step reset of Bedwars settings only",
              true, [&]() {
                const ULONGLONG now = GetTickCount64();
                if (resetArmedAt != 0 && now - resetArmedAt < 8000) {
                  BwConfig::save(BwConfig::Settings{});
                  resetArmedAt = 0;
                } else {
                  resetArmedAt = now;
                }
              });
  }

  if (section(1, "Player Alerts")) {
    moduleRow(Module::ArmorAlerts, "Named armor-tier transitions; teammates excluded");
    moduleRow(Module::UpgradeAlerts, "One team-level Sharpness alert from enchanted swords");
    moduleRow(Module::ConsumeAlerts, "Named visible item-use transitions");
    moduleRow(Module::ItemAlerts, "Named held-item, potion, and knockback-stick alerts");
    const char *visibility = settings.visibilityMode == VisibilityMode::RangeOnly
                                 ? "Range Only"
                                 : settings.visibilityMode == VisibilityMode::LineOfSight
                                       ? "Line of Sight"
                                       : "Camera View";
    actionRow(std::string("Visibility Mode: ") + visibility,
              "Click to cycle range, raycast, and camera/FOV filtering", true,
              [&]() {
                const int next =
                    (static_cast<int>(BwConfig::getVisibilityMode()) + 1) % 3;
                BwConfig::setVisibilityMode(static_cast<VisibilityMode>(next));
              });
    sliderRow("Alert Range", settings.playerAlertRange, 4.0F, 256.0F, 700,
              "m", true,
              [](float value) { BwConfig::setPlayerAlertRange(value); });
    // Only meaningful in Camera View mode, so it only takes up room when
    // that mode is actually selected instead of always being on screen.
    if (settings.visibilityMode == VisibilityMode::CameraView) {
      sliderRow("Camera Region", settings.cameraViewDegrees, 30.0F, 170.0F,
                701, " deg", true,
                [](float value) { BwConfig::setCameraViewDegrees(value); });
    }
    sliderRow("Alert Cooldown",
              static_cast<float>(settings.playerAlertCooldownMs), 250.0F,
              60000.0F, 702, "ms", true, [](float value) {
                BwConfig::setPlayerAlertCooldownMs(
                    static_cast<int>(value + 0.5F));
              });
  }

  if (section(2, "Team Tracking")) {
    moduleRow(Module::UpgradeHud, "Chat-confirmed local upgrades and eight-team state");
    moduleRow(Module::TrapNotifier,
              "Confirmed trap messages; reminders require explicit missing state");
    toggleRow("Short Upgrade Labels", "Compact labels in Team State HUD",
              settings.shortUpgradeLabels, 502, true, true,
              [](bool enabled) { BwConfig::setShortUpgradeLabels(enabled); });
    sliderRow("Trap Reminder",
              static_cast<float>(settings.trapReminderSeconds), 15.0F, 600.0F,
              703, "s", true, [](float value) {
                BwConfig::setTrapReminderSeconds(
                    static_cast<int>(value + 0.5F));
              });
  }

  if (section(3, "Beds and Placement")) {
    moduleRow(Module::BedTracker, "Own-bed distance and loaded two-block bed state");
    moduleRow(Module::AntiMisplace, "Eight-cell own-bed shell");
    // The row below is the one place that shows the full Anti Misplace
    // status and reason -- it used to also be repeated a second time in
    // the Diagnostics section further down; that duplicate is gone now.
    actionRow("Anti Misplace: " +
                  (runtime.antiMisplaceStatus.empty()
                       ? std::string("Placement hook unavailable")
                       : runtime.antiMisplaceStatus),
              runtime.antiMisplaceDetails, false, []() {});
    sliderRow("Bed Warning Range", settings.bedWarningRange, 5.0F, 128.0F,
              710, "m", true,
              [](float value) { BwConfig::setBedWarningRange(value); });
    sliderRow("Bed Scan Range", settings.bedMaximumRange, 16.0F, 160.0F,
              711, "m", true,
              [](float value) { BwConfig::setBedMaximumRange(value); });
    sliderRow("Bed Scan Interval",
              static_cast<float>(settings.bedScanIntervalMs) / 1000.0F,
              5.0F, 30.0F, 712, "s", true, [](float value) {
                BwConfig::setBedScanIntervalMs(
                    static_cast<int>(value * 1000.0F + 0.5F));
              });
  }

  if (section(4, "HUD and Layout")) {
    // Position is set by turning this on and dragging each HUD where you
    // want it on screen -- that already writes the same x/y this section
    // used to also expose as separate percentage sliders, so those were
    // just a slower, less direct way to do the same thing and have been
    // removed. Scale has no drag equivalent, so it stays as a slider.
    toggleRow("Layout Preview", "Show all HUD previews and drag them on screen",
              Render::BedwarsOverlay::isLayoutMode(), 503, true, false,
              [](bool enabled) {
                Render::BedwarsOverlay::setLayoutMode(enabled);
              });
    for (std::size_t i = 0; i < kHudCount; ++i) {
      const HudId hud = static_cast<HudId>(i);
      HudLayout layout = BwConfig::getHudLayout(hud);
      toggleRow(hudName(hud), "Independent HUD visibility", layout.visible,
                600 + static_cast<int>(i), true, true,
                [hud, layout](bool enabled) mutable {
                  layout.visible = enabled;
                  BwConfig::setHudLayout(hud, layout);
                });
      sliderRow((std::string(hudName(hud)) + " Scale").c_str(),
                layout.scale * 100.0F, 50.0F, 250.0F,
                732 + static_cast<int>(i) * 3, "%", true,
                [hud, layout](float value) mutable {
                  layout.scale = value / 100.0F;
                  BwConfig::setHudLayout(hud, layout);
                });
      actionRow(std::string("Reset ") + hudName(hud) + " Position",
                "Restore the safe default position and scale", true,
                [hud]() { BwConfig::resetHudLayout(hud); });
    }
    actionRow("Reset All HUD Positions", "Restore non-overlapping defaults", true,
              []() { BwConfig::resetAllHudLayouts(); });
  }

  if (section(5, "Resources")) {
    moduleRow(Module::ResourceTracker, "Bounded local inventory totals and deltas");
    moduleRow(Module::PickupAlerts,
              "Local positive resource-count changes; initial scan is baseline");
    toggleRow("Separate Resource Alerts",
              "One notification per simultaneous resource increase",
              settings.stackedResourceAlerts, 504, true, true,
              [](bool enabled) { BwConfig::setStackedResourceAlerts(enabled); });
    for (std::size_t i = 0; i < kResourceCount; ++i) {
      const Resource resource = static_cast<Resource>(i);
      const std::string title = std::string(resourceName(resource)) + " Tracking";
      toggleRow(title.c_str(), "Include in resource totals and alerts",
                settings.resources[i], 620 + static_cast<int>(i), true, true,
                [resource](bool enabled) {
                  BwConfig::setResourceEnabled(resource, enabled);
                });
    }
  }

  if (section(6, "Notifications")) {
    sliderRow("Default Duration", settings.defaultNotificationSeconds, 1.0F,
              15.0F, 760, "s", true,
              [](float value) { BwConfig::setDefaultNotificationSeconds(value); });
    sliderRow("Team Upgrade Duration", settings.importantNotificationSeconds,
              1.0F, 15.0F, 761, "s", true, [](float value) {
                BwConfig::setImportantNotificationSeconds(value);
              });
    sliderRow("Player Item Duration", settings.playerNotificationSeconds,
              1.0F, 15.0F, 762, "s", true, [](float value) {
                BwConfig::setPlayerNotificationSeconds(value);
              });
    sliderRow("Warning Duration", settings.warningNotificationSeconds, 1.0F,
              15.0F, 763, "s", true,
              [](float value) { BwConfig::setWarningNotificationSeconds(value); });
    sliderRow("Maximum Visible",
              static_cast<float>(settings.maximumVisibleNotifications), 1.0F,
              10.0F, 764, "", true, [](float value) {
                BwConfig::setMaximumVisibleNotifications(
                    static_cast<int>(value + 0.5F));
              });
  }

  if (section(7, "Map Height")) {
    moduleRow(Module::EventTimers, "Public schedule with live scoreboard preference");
    moduleRow(Module::HeightOverlay, "Automatic reviewed per-map placement limit");
    toggleRow("Only Next Event", "Hide later events from Event Timer HUD",
              settings.onlyNextEvent, 505, true, true,
              [](bool enabled) { BwConfig::setOnlyNextEvent(enabled); });
    toggleRow("Dynamic Timer Color", "Warm color as an event approaches",
              settings.dynamicTimerColor, 506, true, true,
              [](bool enabled) { BwConfig::setDynamicTimerColor(enabled); });
    toggleRow("Dynamic Height Color", "Warm color near the placement limit",
              settings.dynamicHeightColor, 507, true, true,
              [](bool enabled) { BwConfig::setDynamicHeightColor(enabled); });
    const std::string currentMap = runtime.mapName;
    const auto resolved = resolveMapHeight(currentMap, settings.mapPlacementOverrides);
    actionRow("Detected Map: " + (currentMap.empty() ? std::string("Unknown") : currentMap),
              resolved.maximumPlacementY
                  ? "Effective placement Y: " +
                        std::to_string(*resolved.maximumPlacementY) +
                        (resolved.overridden ? " (override)" : " (built-in)")
                  : "No reviewed value; configure an override",
              false, []() {});
    const float overrideValue = resolved.maximumPlacementY
                                    ? static_cast<float>(*resolved.maximumPlacementY)
                                    : 100.0F;
    sliderRow("Current Map Override", overrideValue, 1.0F, 511.0F, 770, "",
              true, [currentMap](float value) {
                if (!currentMap.empty())
                  BwConfig::setMapPlacementOverride(
                      currentMap, static_cast<int>(value + 0.5F));
              });
    actionRow("Reset Current Map Override", "Return to the reviewed built-in value",
              !currentMap.empty(), [currentMap]() {
                BwConfig::resetMapPlacementOverride(currentMap);
              });
  }

  if (section(8, "Diagnostics and Availability")) {
    toggleRow("Debug Diagnostics",
              "Rate-limited lifecycle, filter, hook, queue, and map reasons",
              settings.debug, 508, true, false,
              [](bool enabled) { BwConfig::setDebugEnabled(enabled); });
    moduleRow(Module::ShopHelper,
              "Unavailable until a reliable shop-container render/click hook exists");
  }
}

} // namespace Render::Tabs
