#include "Tabs.h"
#include "../State.h"
#include "../Theme.h"
#include "../Helpers.h"
#include "../../Render/RenderUtils.h"
#include "../../Render/NotificationManager.h"
#include "../../Config/Config.h"
#include "../../Logic/BedDefense/BedDefenseManager.h"
#include "../../Logic/BlockHitSound.h"
#include "../../Utils/ReplaySpammer.h"
#include <cmath>
#include <cstdio>
#include <gl/GL.h>
#include <string>

namespace Render {
namespace Tabs {

void renderUtils(TabCtx &ctx) {
  using namespace ClickGUIState;
  using namespace ClickGUITheme;
  const float mainX = ctx.mainX;
  const float cx    = ctx.cx;
  float      &cy    = ctx.cy;
  const float mx    = ctx.mx;
  const float my    = ctx.my;
  const bool  lClick = ctx.lClick;
  const bool  clickEvent = ctx.clickEvent;
  const float alpha = ctx.alpha;

  g_guiFont.drawString(cx, cy, "Utilities", applyAlpha(0xFFFFFFFF, alpha));
  cy += 40;
  bool hCard = isHovered(mx, my, mainX + 190, cy - 10, g_w - 210, 95);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy - 10, g_w - 210, 95, hCard, alpha);
  glEnable(GL_TEXTURE_2D);

  drawSectionLabel(cx, cy, "Bed Defense", alpha);

  g_guiFont.drawString(cx, cy + 18,
                       "X-Ray style outlines for bed defense blocks",
                       applyAlpha(0xFFA0A0A5, alpha));
  g_guiFont.drawString(cx, cy + 42,
                       "WARNING: THIS PROVIDES AN UNFAIR ADVANTAGE.",
                       applyAlpha(0xFFFF5555, alpha), 0.4f);
  g_guiFont.drawString(cx, cy + 54,
                       "YOU WILL BE BLACKLISTED IF CAUGHT. USE AT OWN RISK.",
                       applyAlpha(0xFFFF5555, alpha), 0.4f);

  bool enabled = Config::isBedDefenseEnabled();
  glDisable(GL_TEXTURE_2D);
  float swX = mainX + g_w - 65;
  drawSwitch(0, swX, cy + 15, enabled, hCard, alpha);
  glEnable(GL_TEXTURE_2D);
  if (clickEvent && hCard) {
    bool newState = !enabled;
    Config::setBedDefenseEnabled(newState);
    if (newState)
      BedDefense::BedDefenseManager::getInstance()->enable();
    else
      BedDefense::BedDefenseManager::getInstance()->disable();

    NotificationManager::getInstance()->add(
        "Module", newState ? "Bed Defense Activated" : "Bed Defense Disabled",
        newState ? NotificationType::Success : NotificationType::Warning);
  }
  cy += 115;

  g_guiFont.drawString(cx, cy, "Chat Bypasser",
                       applyAlpha(0xFFFFFFFF, alpha));

  bool hBypass = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 85);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, 85, hBypass, alpha);

  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(cx, cy + 40, "Bypass Chat Filter",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(
      cx, cy + 56, "Allows sending messages that would normally be blocked",
      applyAlpha(0xFFA0A0A5, alpha), 0.45f);

  bool bypassEnabled = Config::isChatBypasserEnabled();
  glDisable(GL_TEXTURE_2D);
  float bypassSwX = mainX + g_w - 65;
  drawSwitch(14, bypassSwX, cy + 40, bypassEnabled, hBypass && (my < cy + 65),
             alpha);
  glEnable(GL_TEXTURE_2D);

  bool hSmart = hBypass && (my >= cy + 65);
  bool smartEnabled = Config::isSmartChatBypassEnabled();
  float smartAlpha = alpha * (bypassEnabled ? 1.0f : 0.4f);

  g_guiFont.drawString(cx + 10, cy + 85, "Smart Mode",
                       applyAlpha(0xFFFFFFFF, smartAlpha), 0.42f);
  glDisable(GL_TEXTURE_2D);
  drawSwitch(25, bypassSwX, cy + 82, smartEnabled, hSmart && bypassEnabled,
             smartAlpha);
  glEnable(GL_TEXTURE_2D);

  if (clickEvent && hBypass) {
    if (my < cy + 65) {
      Config::setChatBypasserEnabled(!bypassEnabled);
      NotificationManager::getInstance()->add(
          "Utils", !bypassEnabled ? "Bypasser Enabled" : "Bypasser Disabled",
          !bypassEnabled ? NotificationType::Success
                         : NotificationType::Warning);
    } else if (bypassEnabled) {
      Config::setSmartChatBypassEnabled(!smartEnabled);
    }
  }
  cy += 135;

  drawSectionLabel(cx, cy, "Faster Stats", alpha);
  bool hNicked = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, 60, hNicked, alpha);

  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(cx, cy + 40, "Direct UUID Fetching",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(cx, cy + 58, "Use direct game UUIDs for instant stats",
                       applyAlpha(0xFFA0A0A5, alpha));

  bool nickedBypass = Config::isNickedBypass();
  glDisable(GL_TEXTURE_2D);
  float nickSwX = mainX + g_w - 65;
  drawSwitch(20, nickSwX, cy + 40, nickedBypass, hNicked, alpha);
  glEnable(GL_TEXTURE_2D);
  if (clickEvent && hNicked) {
    Config::setNickedBypass(!nickedBypass);
    NotificationManager::getInstance()->add(
        "Utils",
        !nickedBypass ? "Direct UUID Fetching Enabled"
                      : "Direct UUID Fetching Disabled",
        !nickedBypass ? NotificationType::Success
                      : NotificationType::Warning);
  }
  cy += 110;

  drawSectionLabel(cx, cy, "Raw Mouse Fix", alpha);
  bool hMouse = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, 60, hMouse, alpha);

  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(cx, cy + 40, "Raw Mouse Input",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(cx, cy + 58, "Fixes choppy mouse movement in-game",
                       applyAlpha(0xFFA0A0A5, alpha));

  bool mouseFix = Config::isRawMouseFixEnabled();
  glDisable(GL_TEXTURE_2D);
  float mouseSwX = mainX + g_w - 65;
  drawSwitch(28, mouseSwX, cy + 40, mouseFix, hMouse, alpha);
  glEnable(GL_TEXTURE_2D);
  if (clickEvent && hMouse) {
    Config::setRawMouseFixEnabled(!mouseFix);
    NotificationManager::getInstance()->add(
        "Utils",
        !mouseFix ? "Raw Mouse Fix Enabled"
                  : "Raw Mouse Fix Disabled",
        !mouseFix ? NotificationType::Success
                  : NotificationType::Warning);
  }
  cy += 110;

  drawSectionLabel(cx, cy, "Nick Score Alerts", alpha);
  const float nickScoreCardH = 298.0f;
  const bool hNickScore =
      isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, nickScoreCardH);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, nickScoreCardH,
                hNickScore, alpha);
  glEnable(GL_TEXTURE_2D);

  g_guiFont.drawString(cx, cy + 40, "Score generated /nick names",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(
      cx, cy + 58,
      "Reads the open book, scores the name, stops when one passes",
      applyAlpha(0xFFA0A0A5, alpha), 0.43f);

  float nickThreshold =
      static_cast<float>(Config::getNickScoreThreshold());
  char thresholdText[16]{};
  snprintf(thresholdText, sizeof(thresholdText), "%d",
           static_cast<int>(nickThreshold));
  g_guiFont.drawString(cx + 10, cy + 88, "Stop score",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  const float thresholdTextWidth =
      g_guiFont.getStringWidth(thresholdText) * (0.42f / 0.5f);
  g_guiFont.drawString(mainX + g_w - 38.0f - thresholdTextWidth, cy + 88,
                       thresholdText, applyAlpha(accent(), alpha), 0.42f);
  if (drawSlider(3070, cx + 82.0f, cy + 96.0f, g_w - 328.0f, 8.0f,
                 nickThreshold, 0.0f, 100.0f, mx, my,
                 lClick && hNickScore, alpha)) {
    Config::setNickScoreThreshold(
        static_cast<int>(std::lround(nickThreshold)));
  }

  const float nickScoreSwX = mainX + g_w - 65;
  const bool nickPing = Config::isNickScorePingEnabled();
  const bool hNickPing =
      hNickScore && my >= cy + 112.0f && my < cy + 148.0f;
  g_guiFont.drawString(cx + 10, cy + 124, "Ping when a name passes",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  glDisable(GL_TEXTURE_2D);
  drawSwitch(70, nickScoreSwX, cy + 121, nickPing, hNickPing, alpha);
  glEnable(GL_TEXTURE_2D);

  const bool alertEvery = Config::isNickScoreAlertEveryEnabled();
  const bool hAlertEvery =
      hNickScore && my >= cy + 148.0f && my < cy + 184.0f;
  g_guiFont.drawString(cx + 10, cy + 160,
                       alertEvery ? "Alert every nick" : "Alert only found",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  glDisable(GL_TEXTURE_2D);
  drawSwitch(71, nickScoreSwX, cy + 157, alertEvery, hAlertEvery, alpha);
  glEnable(GL_TEXTURE_2D);

  const bool autoReroll = Config::isNickRollAutoRerollEnabled();
  const bool hAutoReroll =
      hNickScore && my >= cy + 184.0f && my < cy + 220.0f;
  g_guiFont.drawString(cx + 10, cy + 196, "Auto TRY AGAIN until one passes",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  glDisable(GL_TEXTURE_2D);
  drawSwitch(72, nickScoreSwX, cy + 193, autoReroll, hAutoReroll, alpha);
  glEnable(GL_TEXTURE_2D);

  float rerollDelay =
      static_cast<float>(Config::getNickRollRerollDelayMs());
  char delayText[16]{};
  snprintf(delayText, sizeof(delayText), "%dms",
           static_cast<int>(rerollDelay));
  g_guiFont.drawString(cx + 10, cy + 228, "Delay",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  const float delayTextWidth =
      g_guiFont.getStringWidth(delayText) * (0.42f / 0.5f);
  g_guiFont.drawString(mainX + g_w - 38.0f - delayTextWidth, cy + 228,
                       delayText, applyAlpha(accent(), alpha), 0.42f);
  if (drawSlider(3071, cx + 82.0f, cy + 236.0f, g_w - 328.0f, 8.0f,
                 rerollDelay, 250.0f, 5000.0f, mx, my,
                 lClick && hNickScore, alpha)) {
    Config::setNickRollRerollDelayMs(
        static_cast<int>(std::lround(rerollDelay)));
  }

  float rerollCap = static_cast<float>(Config::getNickRollRerollCap());
  char capText[16]{};
  snprintf(capText, sizeof(capText), "%d", static_cast<int>(rerollCap));
  g_guiFont.drawString(cx + 10, cy + 262, "Max rerolls",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  const float capTextWidth =
      g_guiFont.getStringWidth(capText) * (0.42f / 0.5f);
  g_guiFont.drawString(mainX + g_w - 38.0f - capTextWidth, cy + 262, capText,
                       applyAlpha(accent(), alpha), 0.42f);
  if (drawSlider(3072, cx + 82.0f, cy + 270.0f, g_w - 328.0f, 8.0f, rerollCap,
                 10.0f, 2000.0f, mx, my, lClick && hNickScore, alpha)) {
    Config::setNickRollRerollCap(static_cast<int>(std::lround(rerollCap)));
  }

  if (clickEvent && hNickPing)
    Config::setNickScorePingEnabled(!nickPing);
  else if (clickEvent && hAlertEvery)
    Config::setNickScoreAlertEveryEnabled(!alertEvery);
  else if (clickEvent && hAutoReroll)
    Config::setNickRollAutoRerollEnabled(!autoReroll);
  cy += 348;

  drawSectionLabel(cx, cy, "Block-Hit Sound (Client Heuristic)", alpha);
  const float blockSoundCardH = 276.0f;
  bool hBlockSound =
      isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, blockSoundCardH);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, blockSoundCardH,
                hBlockSound, alpha);
  glEnable(GL_TEXTURE_2D);

  g_guiFont.drawString(cx, cy + 40, "Correlated Block-Hit Sound",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(
      cx, cy + 58,
      "Client heuristic only; Minecraft sends no confirmed block result",
      applyAlpha(0xFFA0A0A5, alpha), 0.43f);

  const float blockSoundSwX = mainX + g_w - 65;
  bool blockSoundEnabled = Config::isBlockHitSoundEnabled();
  bool hBlockSoundMaster = hBlockSound && my >= cy + 34 && my < cy + 76;
  glDisable(GL_TEXTURE_2D);
  drawSwitch(60, blockSoundSwX, cy + 40, blockSoundEnabled,
             hBlockSoundMaster, alpha);
  glEnable(GL_TEXTURE_2D);

  const std::string &source = Config::getBlockHitSoundSource();
  g_guiFont.drawString(cx + 10, cy + 88, "Sound source",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  const float sourceX = mainX + g_w - 190.0f;
  const float sourceY = cy + 80.0f;
  const float sourceW = 61.0f;
  const float sourceH = 25.0f;
  const bool hDefault =
      isHovered(mx, my, sourceX, sourceY, sourceW, sourceH);
  const bool hCustom =
      isHovered(mx, my, sourceX + sourceW + 6.0f, sourceY, sourceW, sourceH);
  glDisable(GL_TEXTURE_2D);
  drawThemeButton(sourceX, sourceY, sourceW, sourceH, hDefault,
                  source == "Default", alpha);
  drawThemeButton(sourceX + sourceW + 6.0f, sourceY, sourceW, sourceH, hCustom,
                  source == "Custom", alpha);
  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(sourceX + 8.0f, sourceY + 6.0f, "Default",
                       applyAlpha(source == "Default" ? accent()
                                                       : textSecondary(),
                                  alpha),
                       0.38f);
  g_guiFont.drawString(sourceX + sourceW + 14.0f, sourceY + 6.0f, "Custom",
                       applyAlpha(source == "Custom" ? accent()
                                                      : textSecondary(),
                                  alpha),
                       0.38f);
  if (clickEvent && hDefault) Config::setBlockHitSoundSource("Default");
  if (clickEvent && hCustom) Config::setBlockHitSoundSource("Custom");

  float blockSoundVolume = Config::getBlockHitSoundVolume();
  char volumeText[16]{};
  snprintf(volumeText, sizeof(volumeText), "%d%%",
           static_cast<int>(blockSoundVolume + 0.5f));
  g_guiFont.drawString(cx + 10, cy + 125, "Volume",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  const float volumeTextWidth =
      g_guiFont.getStringWidth(volumeText) * (0.42f / 0.5f);
  g_guiFont.drawString(mainX + g_w - 38.0f - volumeTextWidth, cy + 125,
                       volumeText, applyAlpha(accent(), alpha), 0.42f);
  if (drawSlider(3060, cx + 76.0f, cy + 132.0f, g_w - 322.0f, 8.0f,
                 blockSoundVolume, 0.0f, 100.0f, mx, my,
                 lClick && hBlockSound, alpha)) {
    Config::setBlockHitSoundVolume(blockSoundVolume);
  }

  std::string filename = Config::getBlockHitSoundFilename();
  if (filename.size() > 34U) filename = filename.substr(0U, 31U) + "...";
  g_guiFont.drawString(cx + 10, cy + 162, "Selected WAV",
                       applyAlpha(0xFFFFFFFF, alpha), 0.42f);
  g_guiFont.drawString(cx + 104, cy + 162, filename.c_str(),
                       applyAlpha(textSecondary(), alpha), 0.40f);

  const char *actionLabels[] = {"Next", "Reload", "Preview", "Folder"};
  const float actionX = cx + 10.0f;
  const float actionY = cy + 185.0f;
  const float actionGap = 7.0f;
  const float actionAreaW = g_w - 260.0f;
  const float actionW = (actionAreaW - actionGap * 3.0f) / 4.0f;
  for (int index = 0; index < 4; ++index) {
    const float buttonX = actionX + index * (actionW + actionGap);
    const bool hovered =
        isHovered(mx, my, buttonX, actionY, actionW, 27.0f);
    glDisable(GL_TEXTURE_2D);
    drawThemeButton(buttonX, actionY, actionW, 27.0f, hovered, false, alpha);
    glEnable(GL_TEXTURE_2D);
    const float labelW = g_guiFont.getStringWidth(actionLabels[index]) *
                         (0.38f / 0.5f);
    g_guiFont.drawString(buttonX + actionW * 0.5f - labelW * 0.5f,
                         actionY + 7.0f, actionLabels[index],
                         applyAlpha(hovered ? textPrimary() : textSecondary(),
                                    alpha),
                         0.38f);
    if (clickEvent && hovered) {
      if (index == 0)
        BlockHitSound::requestSelectNextCustomSound();
      else if (index == 1)
        BlockHitSound::requestCustomSoundReload();
      else if (index == 2)
        BlockHitSound::requestPreview();
      else if (!BlockHitSound::openSoundsDirectory())
        NotificationManager::getInstance()->add(
            "Block-Hit Sound", "Could not open the sounds folder",
            NotificationType::Warning);
    }
  }

  bool blockSoundDebug = Config::isBlockHitSoundDebugEnabled();
  bool hBlockSoundDebug =
      hBlockSound && my >= cy + 224.0f && my < cy + 266.0f;
  const float blockSoundDebugAlpha =
      alpha * (blockSoundEnabled ? 1.0f : 0.4f);
  g_guiFont.drawString(cx + 10, cy + 239, "Debug trigger/rejection reasons",
                       applyAlpha(0xFFFFFFFF, blockSoundDebugAlpha), 0.42f);
  glDisable(GL_TEXTURE_2D);
  drawSwitch(61, blockSoundSwX, cy + 236, blockSoundDebug,
             hBlockSoundDebug && blockSoundEnabled, blockSoundDebugAlpha);
  glEnable(GL_TEXTURE_2D);

  if (clickEvent && hBlockSoundMaster) {
    Config::setBlockHitSoundEnabled(!blockSoundEnabled);
    NotificationManager::getInstance()->add(
        "Utils",
        !blockSoundEnabled ? "Heuristic Block-Hit Sound Enabled"
                           : "Heuristic Block-Hit Sound Disabled",
        !blockSoundEnabled ? NotificationType::Success
                           : NotificationType::Warning);
  } else if (clickEvent && hBlockSoundDebug && blockSoundEnabled) {
    Config::setBlockHitSoundDebugEnabled(!blockSoundDebug);
  }
  cy += 326;

  g_guiFont.drawString(cx, cy, "Replay Automations",
                       applyAlpha(0xFFFFFFFF, alpha));
  bool hReplay = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, 60, hReplay, alpha);

  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(cx, cy + 40, "Replay Report Spammer",
                       applyAlpha(0xFF808085, alpha));
  g_guiFont.drawString(cx, cy + 58,
                       "Disabled",
                       applyAlpha(0xFFA0A0A5, alpha));

  if (Utils::ReplaySpammer::getInstance().isEnabled())
    Utils::ReplaySpammer::getInstance().toggle();
  glDisable(GL_TEXTURE_2D);
  float replaySwX = mainX + g_w - 65;
  drawSwitch(21, replaySwX, cy + 40, false, false, alpha * 0.4f);
  glEnable(GL_TEXTURE_2D);
  if (clickEvent && hReplay) {
    NotificationManager::getInstance()->add(
        "Utils", "Replay Spammer is disabled",
        NotificationType::Warning);
  }
  cy += 110;

  g_guiFont.drawString(cx, cy, "Aurora Denicker",
                       applyAlpha(0xFFFFFFFF, alpha));
  bool hDenick = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, 60);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, 60, hDenick, alpha);

  glEnable(GL_TEXTURE_2D);
  g_guiFont.drawString(cx, cy + 40, "Number Denicker",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(cx, cy + 58,
                       "Reveal nicks via game statistics",
                       applyAlpha(0xFFA0A0A5, alpha));

  bool denickEnabled = Config::isNumberDenickerEnabled();
  glDisable(GL_TEXTURE_2D);
  float denickSwX = mainX + g_w - 65;
  drawSwitch(22, denickSwX, cy + 40, denickEnabled, hDenick, alpha);
  glEnable(GL_TEXTURE_2D);

  if (clickEvent && hDenick) {
    Config::setNumberDenickerEnabled(!denickEnabled);
    Config::save();
    NotificationManager::getInstance()->add(
        "Utils",
        !denickEnabled ? "Number Denicker Enabled"
                       : "Number Denicker Disabled",
        !denickEnabled ? NotificationType::Success
                       : NotificationType::Warning);
  }
  cy += 110;


  drawSectionLabel(cx, cy, "Anticheat", alpha);
  const float acCardH = 262.0f;
  bool hAc = isHovered(mx, my, mainX + 190, cy + 30, g_w - 210, acCardH);
  glDisable(GL_TEXTURE_2D);
  drawThemeCard(mainX + 190, cy + 30, g_w - 210, acCardH, hAc, alpha);
  glEnable(GL_TEXTURE_2D);

  g_guiFont.drawString(cx, cy + 40, "Detect Cheaters (BETA)",
                       applyAlpha(0xFFFFFFFF, alpha));
  g_guiFont.drawString(
      cx, cy + 58,
      "Four client-side checks: NoSlow, AutoBlock, Eagle, Scaffold",
      applyAlpha(0xFFA0A0A5, alpha), 0.45f);

  bool acEnabled = Config::isAnticheatEnabled();
  glDisable(GL_TEXTURE_2D);
  float acSwX = mainX + g_w - 65;
  bool hAcMaster = hAc && my < cy + 70;
  drawSwitch(40, acSwX, cy + 40, acEnabled, hAcMaster, alpha);
  glEnable(GL_TEXTURE_2D);
  if (clickEvent && hAcMaster) {
    Config::setAnticheatEnabled(!acEnabled);
    NotificationManager::getInstance()->add(
        "Utils", !acEnabled ? "Anticheat Enabled" : "Anticheat Disabled",
        !acEnabled ? NotificationType::Success : NotificationType::Warning);
  }

  float rowAlpha = alpha * (acEnabled ? 1.0f : 0.45f);
  struct AcSub {
    const char *label;
    bool (*get)();
    void (*set)(bool);
    int switchId;
  };
  static const AcSub kSubs[] = {
      {"NoSlow", &Config::isAnticheatNoSlowEnabled,
       &Config::setAnticheatNoSlowEnabled, 41},
      {"AutoBlock", &Config::isAnticheatAutoBlockEnabled,
       &Config::setAnticheatAutoBlockEnabled, 42},
      {"Eagle", &Config::isAnticheatEagleEnabled,
       &Config::setAnticheatEagleEnabled, 43},
      {"Scaffold", &Config::isAnticheatScaffoldEnabled,
       &Config::setAnticheatScaffoldEnabled, 44},
      {"Check Self", &Config::isAnticheatCheckSelfEnabled,
       &Config::setAnticheatCheckSelfEnabled, 46},
  };
  const float subStartY = cy + 84;
  const float subRowH = 32.0f;
  for (size_t i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); ++i) {
    float ry = subStartY + (float)i * subRowH;
    bool hSub = hAc && my >= ry - 2 && my < ry + subRowH - 2;
    bool cur = kSubs[i].get();
    g_guiFont.drawString(cx + 10, ry + 4, kSubs[i].label,
                         applyAlpha(0xFFFFFFFF, rowAlpha), 0.42f);
    glDisable(GL_TEXTURE_2D);
    drawSwitch(kSubs[i].switchId, acSwX, ry + 2, cur, hSub && acEnabled,
               rowAlpha);
    glEnable(GL_TEXTURE_2D);
    if (clickEvent && hSub && acEnabled) {
      kSubs[i].set(!cur);
    }
  }
  cy += (int)acCardH + 25;
}

} // namespace Tabs
} // namespace Render
