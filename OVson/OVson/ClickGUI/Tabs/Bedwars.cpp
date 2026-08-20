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
  // Rows are inset from the group card so the group reads as a container
  // holding its rows, rather than as one more row of the same width.
  const float rowX = panelX + 10.0F;
  const float rowW = panelW - 20.0F;
  const float textX = rowX + 14.0F;
  float &cy = ctx.cy;
  const auto settings = BwConfig::get();
  const auto runtime = Runtime::instance().snapshot();
  const bool master = settings.masterEnabled;

  static std::array<bool, 6> expanded = {true,  true,  false,
                                         false, false, false};
  static ULONGLONG resetArmedAt = 0;

  // ---- Group ---------------------------------------------------------------
  // A collapsible group: a header band, then its rows drawn inset beneath it.
  // Only the header is clickable, so a click anywhere on a row below can never
  // collapse the group out from under the control the user was aiming at.
  auto group = [&](std::size_t id, const char *title, const char *caption) {
    const float y = cy - 8.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, panelX, y, panelW, 44.0F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 44.0F, hover, ctx.alpha, expanded[id]);
    drawChevron(panelX + panelW - 26.0F, y + 22.0F, 5.0F, expanded[id],
                0xFF8FD6FF, ctx.alpha);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(textX, cy, title,
                         applyAlpha(0xFFE8F3FF, ctx.alpha), 0.50F);
    g_guiFont.drawString(textX, cy + 17.0F, caption,
                         applyAlpha(0xFF707078, ctx.alpha), 0.34F);
    if (ctx.clickEvent && hover)
      expanded[id] = !expanded[id];
    cy += expanded[id] ? 50.0F : 44.0F;
    return expanded[id];
  };

  auto groupEnd = [&]() { cy += 12.0F; };

  auto toggleRow = [&](const char *title, const char *description, bool value,
                       int switchId, bool available, bool child, auto setter) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, rowX, y, rowW, 44.0F);
    const bool active = available && (!child || master);
    const float alpha = ctx.alpha * (active ? 1.0F : 0.45F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(rowX, y, rowW, 44.0F, hover, ctx.alpha, value && active);
    drawSwitch(switchId, switchX, cy + 3.0F, value, hover && available, alpha);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(textX, cy, title, applyAlpha(0xFFFFFFFF, alpha),
                         0.44F);
    std::string detail = description;
    if (child && !master)
      detail += "  -  master is off";
    else if (!available)
      detail += "  -  unavailable this session";
    g_guiFont.drawString(textX, cy + 16.0F, detail,
                         applyAlpha(0xFF8A8A92, alpha), 0.34F);
    if (ctx.clickEvent && hover && available)
      setter(!value);
    cy += 48.0F;
  };

  auto sliderRow = [&](const std::string &title, float value, float minimum,
                       float maximum, int sliderId, const char *suffix,
                       auto setter) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, rowX, y, rowW, 42.0F);
    const float alpha = ctx.alpha * (master ? 1.0F : 0.45F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(rowX, y, rowW, 42.0F, hover, ctx.alpha);
    float changedValue = value;
    // The readout sits at the right edge, so the track stops short of it
    // instead of running underneath the digits.
    const bool changed = drawSlider(
        sliderId, textX + 140.0F, cy + 10.0F,
        std::max(70.0F, rowW - 215.0F), 6.0F, changedValue, minimum, maximum,
        ctx.mx, ctx.my, ctx.lClick, ctx.alpha);
    glEnable(GL_TEXTURE_2D);
    char number[40]{};
    std::snprintf(number, sizeof(number), "%.0f%s", value, suffix);
    g_guiFont.drawString(textX, cy + 2.0F, title,
                         applyAlpha(0xFFFFFFFF, alpha), 0.42F);
    g_guiFont.drawString(rowX + rowW - 58.0F, cy + 2.0F, number,
                         applyAlpha(0xFF8FD6FF, alpha), 0.38F);
    if (changed)
      setter(changedValue);
    cy += 46.0F;
  };

  auto actionRow = [&](const std::string &title, const std::string &description,
                       bool available, auto action) {
    const float y = cy - 5.0F;
    const bool hover = isHovered(ctx.mx, ctx.my, rowX, y, rowW, 42.0F);
    const float alpha = ctx.alpha * (available ? 1.0F : 0.45F);
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(rowX, y, rowW, 42.0F, hover && available, ctx.alpha);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(textX, cy, title, applyAlpha(0xFFE7F3FF, alpha),
                         0.42F);
    g_guiFont.drawString(textX, cy + 16.0F, description,
                         applyAlpha(0xFF8A8A92, alpha), 0.34F);
    if (ctx.clickEvent && hover && available)
      action();
    cy += 46.0F;
  };

  auto moduleRow = [&](Module module, const char *description) {
    toggleRow(moduleName(module), description,
              BwConfig::isModuleEnabled(module),
              540 + static_cast<int>(module), isModuleAvailable(module), true,
              [module](bool enabled) {
                BwConfig::setModuleEnabled(module, enabled);
              });
  };

  // ---- Header --------------------------------------------------------------
  drawSectionLabel(ctx.cx, cy, "Bedwars", ctx.alpha);
  cy += 30.0F;
  {
    const std::string status =
        runtime.lifecycleStatus.empty()
            ? (master ? std::string("Waiting for game state")
                      : std::string("Disabled"))
            : runtime.lifecycleStatus;
    const float y = cy - 6.0F;
    glDisable(GL_TEXTURE_2D);
    drawThemeCard(panelX, y, panelW, 40.0F, false, ctx.alpha, master);
    glEnable(GL_TEXTURE_2D);
    g_guiFont.drawString(textX, cy, status,
                         applyAlpha(master ? 0xFF8FD6FF : 0xFF9A9AA0,
                                    ctx.alpha),
                         0.44F);
    const std::string map =
        runtime.mapName.empty() ? std::string("Map unknown")
                                : std::string("Map: ") + runtime.mapName;
    g_guiFont.drawString(textX, cy + 16.0F, map,
                         applyAlpha(0xFF707078, ctx.alpha), 0.34F);
    cy += 50.0F;
  }

  // ---- General -------------------------------------------------------------
  if (group(0, "General", "Master gate, sounds and reset")) {
    toggleRow("Bedwars Tools",
              "Master switch; per-module choices are kept while it is off",
              master, 500, true, false,
              [](bool enabled) { BwConfig::setMasterEnabled(enabled); });
    toggleRow("Alert Sounds", "Play a sound with Bedwars alerts",
              settings.sounds, 501, true, true,
              [](bool enabled) { BwConfig::setSoundsEnabled(enabled); });
    actionRow("Reset Bedwars Settings",
              resetArmedAt != 0 && GetTickCount64() - resetArmedAt < 8000
                  ? "Click again within 8 seconds to confirm"
                  : "Restores defaults for this tab only",
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
  groupEnd();

  // ---- Modules -------------------------------------------------------------
  if (group(1, "Modules", "What Bedwars watches for you")) {
    moduleRow(Module::EventTimers, "Diamond and emerald schedule");
    moduleRow(Module::HeightOverlay, "Build limit and blocks remaining");
    moduleRow(Module::ArmorAlerts, "Enemy armor upgrades");
    moduleRow(Module::UpgradeAlerts, "Enemy team Sharpness");
    moduleRow(Module::ItemAlerts, "Enemy potions, bows and knockback sticks");
    moduleRow(Module::ConsumeAlerts, "Enemies drinking or eating");
    moduleRow(Module::UpgradeHud, "Your team's upgrade state");
    moduleRow(Module::TrapNotifier, "Trap triggers and refill reminders");
    moduleRow(Module::ResourceTracker, "Iron, gold, diamond and emerald totals");
    moduleRow(Module::PickupAlerts, "Resource pickups");
    moduleRow(Module::ShopHelper, "Not available yet");
  }
  groupEnd();

  // ---- Alerts --------------------------------------------------------------
  if (group(2, "Alerts", "Who counts as visible, and how far")) {
    const char *visibility =
        settings.visibilityMode == VisibilityMode::RangeOnly
            ? "Range Only"
            : settings.visibilityMode == VisibilityMode::LineOfSight
                  ? "Line of Sight"
                  : "Camera View";
    actionRow(std::string("Visibility: ") + visibility,
              "Click to cycle range, line of sight and camera view", true,
              [&]() {
                const int next =
                    (static_cast<int>(BwConfig::getVisibilityMode()) + 1) % 3;
                BwConfig::setVisibilityMode(static_cast<VisibilityMode>(next));
              });
    actionRow(std::string("Alert Output: ") +
                  alertOutputName(settings.alertOutput),
              "Click to cycle overlay popups, chat lines, or both", true,
              [&]() {
                const int next =
                    (static_cast<int>(BwConfig::getAlertOutput()) + 1) % 3;
                BwConfig::setAlertOutput(static_cast<AlertOutput>(next));
              });
    sliderRow("Alert Range", settings.playerAlertRange, 4.0F, 256.0F, 700, "m",
              [](float value) { BwConfig::setPlayerAlertRange(value); });
    sliderRow("Trap Reminder",
              static_cast<float>(settings.trapReminderSeconds), 15.0F, 600.0F,
              703, "s", [](float value) {
                BwConfig::setTrapReminderSeconds(
                    static_cast<int>(value + 0.5F));
              });
    toggleRow("Only Next Event", "Show just the next event on the timer HUD",
              settings.onlyNextEvent, 505, true, true,
              [](bool enabled) { BwConfig::setOnlyNextEvent(enabled); });
    for (std::size_t i = 0; i < kResourceCount; ++i) {
      const Resource resource = static_cast<Resource>(i);
      const std::string title = std::string(resourceName(resource)) + " Alerts";
      toggleRow(title.c_str(), "Count this resource in totals and pickups",
                settings.resources[i], 620 + static_cast<int>(i), true, true,
                [resource](bool enabled) {
                  BwConfig::setResourceEnabled(resource, enabled);
                });
    }
  }
  groupEnd();

  // ---- Item Alerts ---------------------------------------------------------
  // One switch per item rather than one blanket toggle: which items are worth
  // interrupting you for is a matter of how you play, and the only person who
  // can answer that is the person reading the alerts. Item Alerts in the
  // Modules group above is still the master -- these only narrow it.
  if (group(5, "Item Alerts", "Which items are worth telling you about")) {
    for (std::size_t i = 1; i < kImportantItemCount; ++i) {
      const ImportantItem item = static_cast<ImportantItem>(i);
      const char *name = importantItemName(item);
      if (!name || !*name)
        continue;
      toggleRow(name, "Alert when an enemy has this", settings.itemAlerts[i],
                700 + static_cast<int>(i), true, true,
                [item](bool enabled) {
                  BwConfig::setItemAlertEnabled(item, enabled);
                });
    }
  }
  groupEnd();

  // ---- HUD -----------------------------------------------------------------
  if (group(3, "HUD", "Which panels show, and how big")) {
    // Position comes from dragging in layout mode, which writes the same x/y
    // a pair of percentage sliders used to; scale has no drag equivalent, so
    // it is the one thing still worth a slider here.
    toggleRow("Layout Mode", "Show every panel and drag it into place",
              Render::BedwarsOverlay::isLayoutMode(), 503, true, false,
              [](bool enabled) {
                Render::BedwarsOverlay::setLayoutMode(enabled);
              });
    for (std::size_t i = 0; i < kHudCount; ++i) {
      const HudId hud = static_cast<HudId>(i);
      HudLayout layout = BwConfig::getHudLayout(hud);
      toggleRow(hudName(hud), "Show this panel", layout.visible,
                600 + static_cast<int>(i), true, true,
                [hud, layout](bool enabled) mutable {
                  layout.visible = enabled;
                  BwConfig::setHudLayout(hud, layout);
                });
      if (layout.visible) {
        sliderRow(std::string(hudName(hud)) + " Scale", layout.scale * 100.0F,
                  50.0F, 250.0F, 732 + static_cast<int>(i) * 3, "%",
                  [hud, layout](float value) mutable {
                    layout.scale = value / 100.0F;
                    BwConfig::setHudLayout(hud, layout);
                  });
      }
    }
    actionRow("Reset HUD Layout", "Restore default positions and scales", true,
              []() { BwConfig::resetAllHudLayouts(); });
  }
  groupEnd();

  // ---- Advanced ------------------------------------------------------------
  if (group(4, "Advanced", "Build limit override and diagnostics")) {
    const auto resolved = resolveMapHeight(runtime.mapName);
    actionRow("Detected Build Limit",
              resolved.maximumPlacementY
                  ? "Y " + std::to_string(*resolved.maximumPlacementY) +
                        " from the built-in map table"
                  : "Unknown map; set a manual limit below",
              false, []() {});
    sliderRow("Manual Build Limit",
              static_cast<float>(settings.heightLimitOverride), 0.0F, 511.0F,
              770, "", [](float value) {
                BwConfig::setHeightLimitOverride(
                    static_cast<int>(value + 0.5F));
              });
    toggleRow("Debug Logging",
              "Rate-limited lifecycle, filter and queue reasons in the log",
              settings.debug, 508, true, false,
              [](bool enabled) { BwConfig::setDebugEnabled(enabled); });
  }
  groupEnd();
}

} // namespace Render::Tabs
