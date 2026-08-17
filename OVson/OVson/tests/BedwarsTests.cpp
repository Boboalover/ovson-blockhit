#include "Logic/Bedwars/BedwarsCore.h"
#include "Logic/Bedwars/BedwarsConfig.h"
#include "../../OVsonLoader/slint_ui/shutdown_coordinator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace OVson::Bedwars;

namespace Config {
static std::string g_testBedwarsSettings;
const std::string &getBedwarsSettingsData() { return g_testBedwarsSettings; }
void setBedwarsSettingsData(const std::string &data) {
  g_testBedwarsSettings = data;
}
} // namespace Config

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SessionObservation activeSession() {
  SessionObservation observation;
  observation.featureEnabled = true;
  observation.worldValid = true;
  observation.playerValid = true;
  observation.onHypixel = true;
  observation.bedwarsMode = true;
  observation.inGame = true;
  observation.localEntityId = 7;
  observation.worldIdentity = 42;
  return observation;
}

void beginKnownGame(Context &context, Tick preGameTick, Tick startTick) {
  auto observation = activeSession();
  observation.inGame = false;
  observation.preGame = true;
  context.observe(observation, preGameTick);
  require(context.observe(activeSession(), startTick).gameStarted,
          "observed pre-game transition did not establish a game clock");
}

void lifecycleStartsAndTracksElapsed() {
  Context context;
  beginKnownGame(context, 900, 1000);
  require(context.phase() == GamePhase::Active, "phase is not active");
  require(context.gameClockKnown(), "observed match start clock is unknown");
  require(context.elapsed(2500) == 1500, "elapsed time is incorrect");
}

void lifecycleLateInjectionDoesNotInventClock() {
  Context context;
  const auto transition = context.observe(activeSession(), 1000);
  require(!transition.gameStarted && !context.gameClockKnown() &&
              context.elapsed(2500) == 0,
          "late injection invented a match start time");
}

void lifecyclePreGameStartsOnExit() {
  Context context;
  auto observation = activeSession();
  observation.inGame = false;
  observation.preGame = true;
  require(!context.observe(observation, 1000).gameStarted,
          "pre-game incorrectly started game");
  observation.inGame = true;
  observation.preGame = false;
  require(context.observe(observation, 1200).gameStarted,
          "leaving pre-game did not start game");
}

void lifecycleRejectsUnknownMode() {
  Context context;
  auto observation = activeSession();
  observation.bedwarsMode = false;
  context.observe(observation, 1000);
  require(context.phase() == GamePhase::Inactive,
          "unknown mode became active");
}

void lifecycleWorldChangeResets() {
  Context context;
  context.observe(activeSession(), 1000);
  auto changed = activeSession();
  changed.worldIdentity = 43;
  const auto transition = context.observe(changed, 2000);
  require(transition.reset, "world change did not reset");
  require(context.generation() == 1, "generation did not advance");
}

void lifecycleClockRegressionResets() {
  Context context;
  context.observe(activeSession(), 1000);
  require(context.observe(activeSession(), 999).reset,
          "clock regression did not reset");
  require(context.elapsed(999) == 0, "regression produced elapsed time");
}

void lifecycleDisableAndReconnect() {
  Context context;
  beginKnownGame(context, 900, 1000);
  auto disabled = activeSession();
  disabled.featureEnabled = false;
  require(context.observe(disabled, 1100).reset,
          "feature disable did not reset");
  const auto reenabled = context.observe(activeSession(), 1200);
  require(!reenabled.gameStarted && !context.gameClockKnown(),
          "re-enable during an active match invented a fresh game clock");
}

void lifecyclePlayerLossDeathAndRespawnPreserveGameClock() {
  Context context;
  beginKnownGame(context, 900, 1000);
  auto unavailable = activeSession();
  unavailable.playerValid = false;
  const auto lost = context.observe(unavailable, 1100);
  require(lost.reset && lost.preservedGameClock,
          "local player loss did not reset transient state");
  const auto restored = context.observe(activeSession(), 1200);
  require(!restored.gameStarted && context.elapsed(1200) == 200,
          "local player recovery restarted the game clock");

  auto dead = activeSession();
  dead.dead = true;
  const auto death = context.observe(dead, 1300);
  require(death.reset && death.preservedGameClock &&
              context.phase() == GamePhase::Spectator,
          "death did not create a preserving reset");
  const auto respawn = context.observe(activeSession(), 1400);
  require(respawn.reset && respawn.preservedGameClock &&
              !respawn.gameStarted && context.elapsed(1400) == 400,
          "respawn restarted the game clock");
}

void lifecycleLeavingGameEndsAndClearsClock() {
  Context context;
  beginKnownGame(context, 900, 1000);
  auto lobby = activeSession();
  lobby.inGame = false;
  lobby.onHypixel = false;
  const auto transition = context.observe(lobby, 1500);
  require(transition.reset && transition.gameEnded &&
              !transition.preservedGameClock && context.elapsed(1500) == 0,
          "leaving the game did not clear the session clock");
}

void timerBoundaryBeforeAndAtEvent() {
  const Tick event = 6ULL * 60ULL * 1000ULL;
  auto before = EventSchedule::countdowns(event - 1, true);
  require(before.size() == 1 && before[0].remainingMs == 1,
          "one millisecond timer boundary failed");
  auto at = EventSchedule::countdowns(event, true);
  require(!at.empty() && at[0].kind == TimedEventKind::EmeraldTwo,
          "completed event was not advanced");
}

void timerNeverGoesNegative() {
  auto events = EventSchedule::countdowns(std::numeric_limits<Tick>::max(), false);
  require(events.empty(), "extreme elapsed time wrapped schedule");
}

void timerParsesScoreboard() {
  const auto event = EventSchedule::parseScoreboardLine("Diamond II in 5:09");
  require(event && event->remainingMs == 309000,
          "scoreboard countdown did not parse");
  require(!EventSchedule::parseScoreboardLine("Diamond II in 2:99"),
          "invalid seconds were accepted");
}

void resourcesIgnoreInitialSnapshot() {
  ResourceMonitor monitor;
  ResourceSnapshot snapshot;
  snapshot.inventoryValid = true;
  snapshot.inventory[0] = 12;
  require(monitor.observe(snapshot).empty(), "initial inventory produced delta");
}

void resourcesReportAdditionsOnly() {
  ResourceMonitor monitor;
  ResourceSnapshot first;
  first.inventoryValid = true;
  first.inventory = {1, 5, 2, 0};
  monitor.observe(first);
  auto second = first;
  second.inventory = {4, 3, 2, 1};
  const auto deltas = monitor.observe(second);
  require(deltas.size() == 2, "resource additions were not isolated");
  require(deltas[0].resource == Resource::Iron && deltas[0].amount == 3,
          "iron delta is wrong");
}

void resourcesIgnoreContainerTransfer() {
  ResourceMonitor monitor;
  ResourceSnapshot first;
  first.inventoryValid = first.enderChestValid = true;
  first.containerOpen = true;
  first.inventory[3] = 1;
  first.enderChest[3] = 5;
  monitor.observe(first);
  auto second = first;
  second.inventory[3] = 3;
  second.enderChest[3] = 3;
  require(monitor.observe(second).empty(),
          "ender chest transfer looked like pickup");
}

void resourcesIgnoreUnknownContainerMovement() {
  ResourceMonitor monitor;
  ResourceSnapshot first;
  first.inventoryValid = true;
  first.containerOpen = true;
  first.inventory[0] = 1;
  monitor.observe(first);
  auto second = first;
  second.inventory[0] = 17;
  require(monitor.observe(second).empty(),
          "unknown container movement looked like a pickup");
}

void resourcesResetDropsHistory() {
  ResourceMonitor monitor;
  ResourceSnapshot snapshot;
  snapshot.inventoryValid = true;
  monitor.observe(snapshot);
  monitor.reset();
  snapshot.inventory[1] = 30;
  require(monitor.observe(snapshot).empty(), "reset retained old inventory");
}

void upgradeParsingAndDeduplication() {
  ChatMonitor monitor;
  auto first = monitor.observe("TEAM UPGRADE > Alex purchased Reinforced Armor", 1000);
  require(first && first->upgrade == UpgradeKind::Protection,
          "upgrade message not parsed");
  require(!monitor.observe("TEAM UPGRADE > Alex purchased Reinforced Armor", 1500),
          "duplicate upgrade was accepted");
  auto later = monitor.observe("TEAM UPGRADE > Alex purchased Reinforced Armor", 2500);
  require(later && later->level == 2, "later upgrade was not accepted");
}

void trapParsingAndReset() {
  ChatMonitor monitor;
  require(monitor.observe("Sam purchased a trap", 1000)->kind ==
              ChatSignal::Kind::TrapQueued,
          "trap purchase not parsed");
  require(!monitor.knownTrap().empty(), "trap state not retained");
  require(monitor.trapState() == TrapState::Queued,
          "trap purchase did not establish queued state");
  require(monitor.observe("TRAP TRIGGERED!", 2000)->kind ==
              ChatSignal::Kind::TrapTriggered,
          "trap trigger not parsed");
  require(monitor.knownTrap().empty(), "trap trigger did not consume trap");
  require(monitor.trapState() == TrapState::Unknown,
          "trap trigger made an unsupported missing-state claim");
  require(monitor.observe("You do not have any trap queued", 3000)->kind ==
              ChatSignal::Kind::TrapMissing &&
              monitor.trapState() == TrapState::Missing,
          "explicit missing-trap evidence was not retained");
  monitor.reset();
  require(monitor.upgrades().protection == 0 &&
              !monitor.upgrades().protectionKnown &&
              monitor.trapState() == TrapState::Unknown,
          "chat reset retained upgrade or trap knowledge");
}

PlayerObservation enemy(int id, const char *identity) {
  PlayerObservation player;
  player.entityId = id;
  player.identity = identity;
  player.distance = 5.0;
  player.lineOfSightKnown = true;
  player.hasLineOfSight = true;
  player.cameraViewKnown = true;
  player.insideCameraView = true;
  return player;
}

void playerMonitorIgnoresInitialEquipment() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.armor = options.items = true;
  auto player = enemy(1, "one");
  player.armor = ArmorTier::Diamond;
  player.heldItem.typeName = "diamond sword";
  require(monitor.observe({player}, options, 1000).empty(),
          "initial visible equipment produced alert");
}

void playerMonitorArmorTransition() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.armor = true;
  auto player = enemy(1, "one");
  player.armor = ArmorTier::Leather;
  monitor.observe({player}, options, 1000);
  player.armor = ArmorTier::Iron;
  const auto alerts = monitor.observe({player}, options, 1500);
  require(alerts.size() == 1 && alerts[0].kind == PlayerAlert::Kind::Armor,
          "armor transition did not alert");
}

void playerMonitorTeamAndRangeFiltering() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.items = true;
  auto teammate = enemy(1, "team");
  teammate.teammateKnown = teammate.teammate = true;
  auto distant = enemy(2, "far");
  distant.distance = 100.0;
  monitor.observe({teammate, distant}, options, 1000);
  teammate.heldItem.typeName = distant.heldItem.typeName = "bow";
  require(monitor.observe({teammate, distant}, options, 2000).empty(),
          "filtered player produced alert");
}

void playerMonitorEntityReuseDoesNotAlert() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.items = true;
  auto first = enemy(1, "first");
  monitor.observe({first}, options, 1000);
  auto replacement = enemy(1, "replacement");
  replacement.heldItem.typeName = "bow";
  require(monitor.observe({replacement}, options, 1100).empty(),
          "entity reuse inherited old equipment");
}

void playerMonitorCooldownAndLaterAlert() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.items = true;
  options.cooldownMs = 1000;
  auto player = enemy(1, "one");
  monitor.observe({player}, options, 1000);
  player.heldItem.typeName = "bow";
  require(monitor.observe({player}, options, 1100).size() == 1,
          "first item transition missing");
  player.heldItem.typeName = "tnt";
  require(monitor.observe({player}, options, 1500).size() == 1,
          "generic cooldown suppressed a distinct important item");
  player.heldItem.typeName = "fireball";
  require(monitor.observe({player}, options, 2200).size() == 1,
          "another distinct important item alert was missing");
}

void playerMonitorPrunesAndCapsBursts() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.capacity = 8;
  std::vector<PlayerObservation> players;
  for (int i = 0; i < 1000; ++i)
    players.push_back(enemy(i, ("player" + std::to_string(i)).c_str()));
  monitor.observe(players, options, 1000);
  require(monitor.trackedCount() == 8, "player map exceeded capacity");
  monitor.observe({}, options, 7001);
  require(monitor.trackedCount() == 8,
          "match-long item memory was discarded on range exit");
}

void bedDistanceUnknownAndBoundary() {
  require(!evaluateBedDistance(std::nullopt, 0, 0, 0, 10).known,
          "unknown bed became known");
  BlockPosition bed{0, 0, 0};
  const auto at = evaluateBedDistance(bed, 3, 4, 0, 5);
  require(at.known && !at.outsideWarningRange,
          "bed boundary was treated as outside");
  require(evaluateBedDistance(bed, 3, 4.001, 0, 5).outsideWarningRange,
          "outside bed boundary was not detected");
}

void antiMisplaceRequiresExactKnownPosition() {
  require(!shouldPreventPlacement(std::nullopt, {0, 1, 0}),
          "unknown bed blocked placement");
  BlockPosition bed{5, 60, 8};
  require(shouldPreventPlacement(bed, {5, 61, 8}),
          "block directly above bed was not rejected");
  require(!shouldPreventPlacement(bed, {6, 61, 8}),
          "adjacent placement was over-blocked");
}

ResourceSnapshot richInventory() {
  ResourceSnapshot snapshot;
  snapshot.inventoryValid = true;
  snapshot.inventory = {20, 4, 2, 1};
  return snapshot;
}

void shopParsesAffordableVisibleLore() {
  const auto offer = evaluateShopOffer(
      "Wool", {"Cost: 4 Iron", "Click to purchase"}, richInventory());
  require(offer.valid && offer.cost == 4 && offer.affordable,
          "valid affordable offer did not parse");
}

void shopRejectsMalformedAndUnknownCurrency() {
  require(!evaluateShopOffer("Thing", {"Cost: many Iron"}, richInventory()).valid,
          "malformed cost was accepted");
  require(!evaluateShopOffer("Thing", {"Cost: 4 Tokens"}, richInventory()).valid,
          "unknown currency was accepted");
}

void shopUnformattedCodesAndDuplicateProtection() {
  const auto offer = evaluateShopOffer(
      "Armor", {"\xC2\xA7" "aCost: 4 Gold", "PERMANENT", "Already owned"},
      richInventory());
  require(offer.valid && offer.affordable && offer.permanent &&
              offer.alreadyOwned,
          "formatted permanent offer did not parse");
  require(!shouldBlockDuplicatePurchase(offer, false),
          "disabled duplicate protection blocked purchase");
  require(shouldBlockDuplicatePurchase(offer, true),
          "enabled duplicate protection missed purchase");
}

void heightKnownAndUnknown() {
  const auto unknown = evaluateHeight(63.9, std::nullopt);
  require(unknown.currentY == 63 && !unknown.limit,
          "unknown height fallback is wrong");
  const auto known = evaluateHeight(63.9, 100);
  require(known.remaining && *known.remaining == 37,
          "height remaining calculation is wrong");
}

void invalidFloatingPointInputsAreSafe() {
  BlockPosition bed{};
  require(!evaluateBedDistance(
               bed, std::numeric_limits<double>::quiet_NaN(), 0, 0, 10)
               .known,
          "NaN distance was accepted");
  require(evaluateHeight(std::numeric_limits<double>::infinity(), 100).currentY ==
              0,
          "infinite height was not sanitized");
}

void configDefaultsAreDisabled() {
  const auto settings = Configuration::deserialize("");
  require(!settings.masterEnabled, "master did not default off");
  for (const bool enabled : settings.modules)
    require(!enabled, "module did not default off");
  require(!settings.debug, "debug did not default off");
}

void configKeepsUnavailableModulesDisabled() {
  const auto settings = Configuration::deserialize(
      "master=1;shopHelper=1;antiMisplace=1;bedTracker=1;");
  require(!settings.modules[static_cast<std::size_t>(Module::ShopHelper)] &&
              settings.modules[static_cast<std::size_t>(Module::AntiMisplace)] &&
              settings.modules[static_cast<std::size_t>(Module::BedTracker)],
          "unavailable modules were enabled from config");
}

void configRoundTripPreservesNamedSettings() {
  Configuration::Settings settings;
  settings.masterEnabled = true;
  settings.modules[static_cast<std::size_t>(Module::EventTimers)] = true;
  settings.modules[static_cast<std::size_t>(Module::BedTracker)] = true;
  settings.playerAlertRange = 47.0F;
  settings.heightLimitOverride = 123;
  const std::string encoded = Configuration::serialize(settings);
  require(encoded.find("eventTimers=1") != std::string::npos,
          "event timer key is not named");
  require(encoded.find("bedTracker=1") != std::string::npos,
          "bed tracker key is not named");
  const auto decoded = Configuration::deserialize(encoded);
  require(decoded.masterEnabled &&
              decoded.modules[static_cast<std::size_t>(Module::EventTimers)] &&
              decoded.modules[static_cast<std::size_t>(Module::BedTracker)],
          "config round trip lost toggles");
  require(decoded.playerAlertRange == 47.0F &&
              decoded.heightLimitOverride == 123,
          "config round trip lost numeric settings");
}

void configRejectsMalformedAndClampsRanges() {
  const auto settings = Configuration::deserialize(
      "master=maybe;debug=yes;timerScale=nan;playerAlertRange=9999;"
      "heightLimitOverride=-20;bedScanIntervalMs=1;bedEspOpacity=-5;"
      "bedEspRed=999;bedEspGreen=-1;bedEspBlue=broken;timerX=0.5junk;");
  require(!settings.masterEnabled && !settings.debug,
          "malformed booleans were accepted");
  require(settings.timerScale == 1.0F, "NaN was accepted");
  require(settings.timerX == 0.02F, "trailing numeric text was accepted");
  require(settings.playerAlertRange == 256.0F,
          "player range was not clamped");
  require(settings.heightLimitOverride == 0,
          "height override was not clamped");
  require(settings.bedScanIntervalMs == 5000,
          "scan interval was not clamped");
  require(Configuration::serialize(settings).find("bedEsp") ==
              std::string::npos,
          "removed renderer settings were serialized again");
}

void allEightTeamsNormalizeAndColorDistinctly() {
  const std::array<TeamId, 8> teams = {
      TeamId::Red, TeamId::Blue, TeamId::Green, TeamId::Yellow,
      TeamId::Aqua, TeamId::White, TeamId::Pink, TeamId::Gray};
  const std::array<const char *, 8> names = {
      "Red", "Blue", "Green", "Yellow", "Aqua", "White", "Pink", "Gray"};
  const std::array<const char *, 8> codes = {
      "c", "9", "a", "e", "b", "f", "d", "7"};
  std::unordered_set<std::uint32_t> colors;
  for (std::size_t i = 0; i < teams.size(); ++i) {
    require(normalizeTeam(names[i]) == teams[i], "standard team did not normalize");
    require(std::string(teamName(teams[i])) == names[i], "team name mismatch");
    require(std::string(teamFormattingCode(teams[i])) == codes[i],
            "team format code mismatch");
    colors.insert(teamArgb(teams[i]));
  }
  require(colors.size() == 8, "standard team colors are not distinct");
  require(normalizeTeam("cyan") == TeamId::Aqua &&
              normalizeTeam("light blue") == TeamId::Aqua,
          "Aqua aliases mapped to Blue");
  require(normalizeTeam("silver") == TeamId::Gray &&
              normalizeTeam("grey team") == TeamId::Gray,
          "Gray aliases failed");
  require(normalizeTeam("orange") == TeamId::Unknown,
          "unknown color was guessed as a team");
}

void teamTrackerHandlesAuthorityReuseExpiryAndReset() {
  TeamTracker tracker;
  tracker.observePlayer("Steve", 10, TeamId::Red, false, 1000, "display");
  tracker.observePlayer("Steve", 10, TeamId::Blue, true, 1100, "scoreboard");
  require(tracker.playerTeam("Steve") == TeamId::Blue,
          "authoritative scoreboard team did not replace weak team");
  tracker.observePlayer("Alex", 10, TeamId::Green, true, 1200, "scoreboard");
  require(tracker.playerTeam("Steve") == TeamId::Unknown &&
              tracker.playerTeam("Alex") == TeamId::Green,
          "entity ID reuse retained the old identity");
  require(tracker.observeSharpness(TeamId::Green, 1300) &&
              !tracker.observeSharpness(TeamId::Green, 1400),
          "team Sharpness did not deduplicate");
  tracker.observeBed(TeamId::Green, BedState::Alive, 1500, "scan");
  require(tracker.team(TeamId::Green).bed == BedState::Alive,
          "team bed state was not retained");
  tracker.expire(4000, 1000);
  require(tracker.playerTeam("Alex") == TeamId::Unknown,
          "stale player binding did not expire");
  tracker.reset();
  for (const auto &team : tracker.teams())
    require(team.bed == BedState::Unknown && !team.sharpnessObserved &&
                team.playerCount == 0,
            "team state survived reset");
}

void teamTrackerIsBoundedAndTracksAllBedStates() {
  TeamTracker tracker;
  for (int i = 0; i < 300; ++i)
    tracker.observePlayer("player" + std::to_string(i), i,
                          static_cast<TeamId>(i % 8), true,
                          static_cast<Tick>(i + 1));
  std::size_t total = 0;
  for (const auto &team : tracker.teams())
    total += team.playerCount;
  require(total <= 128, "team player bindings exceeded the fixed capacity");
  for (std::size_t i = 0; i < 8; ++i)
    tracker.observeBed(static_cast<TeamId>(i),
                       i % 2 == 0 ? BedState::Alive : BedState::Destroyed,
                       5000, "test");
  for (std::size_t i = 0; i < 8; ++i)
    require(tracker.team(static_cast<TeamId>(i)).bed ==
                (i % 2 == 0 ? BedState::Alive : BedState::Destroyed),
            "one of eight team bed states was lost");
  require(tracker.team(TeamId::Unknown).bed == BedState::Unknown,
          "unknown bed ownership was assigned to a standard team");
}

void bedDestructionParsesAllTeamsConservatively() {
  for (std::size_t i = 0; i < 8; ++i) {
    const TeamId team = static_cast<TeamId>(i);
    const std::string message =
        "BED DESTRUCTION > " + std::string(teamName(team)) +
        " Bed was destroyed by Steve!";
    require(parseDestroyedBedTeam(message) == team,
            "standard team bed destruction did not parse");
  }
  require(!parseDestroyedBedTeam("Steve fell into the void") &&
              !parseDestroyedBedTeam(
                  "BED DESTRUCTION > Red Bed hit Blue Bed"),
          "ambiguous or unrelated message changed bed state");
}

PlayerAlertOptions rangeItemOptions() {
  PlayerAlertOptions options;
  options.items = true;
  options.visibility = VisibilityMode::RangeOnly;
  return options;
}

void everyPlayerAlertContainsColoredIdentity() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.armor = options.items = options.consumes = true;
  options.visibility = VisibilityMode::RangeOnly;
  auto player = enemy(21, "Steve");
  player.team = TeamId::Aqua;
  player.teamAuthoritative = true;
  player.armor = ArmorTier::Leather;
  monitor.observe({player}, options, 1000);
  player.armor = ArmorTier::Iron;
  player.heldItem.typeName = "golden apple";
  player.usingItem = true;
  const auto alerts = monitor.observe({player}, options, 2000);
  require(alerts.size() == 2,
          "same-tick held and consume signals produced duplicate alerts");
  for (const auto &alert : alerts) {
    require(alert.text.find("Steve") != std::string::npos,
            "player alert omitted identity");
    require(!alert.segments.empty() && alert.segments.front().text == "Steve" &&
                alert.segments.front().argb == teamArgb(TeamId::Aqua),
            "player identity segment was not team-colored");
  }
  require(alerts[0].text.find("Iron Armor") != std::string::npos,
          "armor tier was not readable");
  require(alerts[1].kind == PlayerAlert::Kind::Consume &&
              alerts[1].text.find("Golden Apple") != std::string::npos,
          "stronger consume signal did not replace the held-item alert");
}

void enchantedSwordProducesOneTeamSharpnessOnly() {
  PlayerMonitor monitor;
  TeamTracker teams;
  PlayerAlertOptions options;
  options.upgrades = true;
  options.visibility = VisibilityMode::RangeOnly;
  auto alice = enemy(1, "Alice");
  auto bob = enemy(2, "Bob");
  alice.team = bob.team = TeamId::Green;
  alice.teamAuthoritative = bob.teamAuthoritative = true;
  alice.heldItem.typeName = bob.heldItem.typeName = "diamond sword";
  monitor.observe({alice, bob}, options, 1000, &teams);
  alice.heldItem.enchanted = bob.heldItem.enchanted = true;
  const auto first = monitor.observe({alice, bob}, options, 1100, &teams);
  require(first.size() == 1 && first[0].kind == PlayerAlert::Kind::Upgrade,
          "enchanted swords did not deduplicate at team level");
  require(first[0].text.find("Alice") != std::string::npos &&
              first[0].text.find("Green Team") != std::string::npos &&
              first[0].text.find("Sharpness") != std::string::npos,
          "Sharpness observation omitted player or team");
  require(first[0].text.find(" II") == std::string::npos &&
              first[0].text.find("level") == std::string::npos,
          "generic glint invented a Sharpness level");
  require(monitor.observe({alice, bob}, options, 5000, &teams).empty(),
          "revealed team Sharpness alerted repeatedly");

  auto armor = enemy(3, "Carol");
  armor.team = TeamId::Red;
  monitor.observe({armor}, options, 6000, &teams);
  armor.heldItem = {"diamond chestplate", "", -1, true};
  require(monitor.observe({armor}, options, 7000, &teams).empty(),
          "generic enchanted armor inferred Protection");
  require(!teams.team(TeamId::Red).sharpnessObserved,
          "non-sword glint changed team upgrade state");
}

void knockbackStickClassificationAndDeduplication() {
  require(!isKnockbackStick({"stick", "", 0, false}),
          "ordinary stick was classified as knockback");
  require(isKnockbackStick({"stick", "Knockback Stick (Right Click)", 0, true}),
          "visibly named knockback stick was not classified");
  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  auto player = enemy(1, "Steve");
  player.team = TeamId::Red;
  monitor.observe({player}, options, 1000);
  player.heldItem = {"stick", "Knockback Stick", 0, true};
  const auto first = monitor.observe({player}, options, 1100);
  require(first.size() == 1 &&
              first[0].kind == PlayerAlert::Kind::KnockbackStick &&
              first[0].text == "Steve has a Knockback Stick",
          "knockback-stick alert was wrong");
  player.heldItem = {};
  monitor.observe({player}, options, 1200);
  player.heldItem = {"stick", "Knockback Stick", 0, true};
  require(monitor.observe({player}, options, 5000).empty(),
          "same player switching back to knockback stick spammed");
}

void potionClassificationDedupAndTransition() {
  require(classifyPotion({"potion", "Potion of Speed", -1, false}) ==
              PotionKind::Unknown,
          "display name alone classified a potion");
  require(classifyPotion({"potion", "Jump V Potion", 11, false}) ==
              PotionKind::Jump,
          "Jump metadata did not classify");
  require(classifyPotion({"potion", "Invisibility Potion", 14, false}) ==
              PotionKind::Invisibility,
          "Invisibility metadata did not classify");
  require(classifyPotion({"potion", "Potion", 2, false}) == PotionKind::Speed,
          "visible potion metadata did not classify");
  require(classifyPotion({"potion", "Potion", -1, false}) ==
              PotionKind::Unknown,
          "unknown potion type was invented");

  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  options.cooldownMs = 60000;
  auto player = enemy(1, "Steve");
  monitor.observe({player}, options, 1000);
  player.heldItem = {"potion", "Speed Potion", 2, false};
  require(monitor.observe({player}, options, 1100).size() == 1,
          "first potion alert missing");
  player.heldItem = {};
  monitor.observe({player}, options, 1200);
  player.heldItem = {"potion", "Speed Potion", 2, false};
  require(monitor.observe({player}, options, 1300).empty(),
          "same potion alerted after slot switching");
  player.heldItem = {"potion", "Invisibility Potion", 14, false};
  const auto changed = monitor.observe({player}, options, 1400);
  require(changed.size() == 1 &&
              changed[0].text.find("Steve") != std::string::npos &&
              changed[0].text.find("Invisibility Potion") != std::string::npos,
          "different potion was suppressed by generic cooldown");
}

void deathForgetsOnlyThatPlayersAlertHistory() {
  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  options.cooldownMs = 60000;
  auto victim = enemy(1, "Steve");
  auto bystander = enemy(2, "Alex");
  monitor.observe({victim, bystander}, options, 1000);
  victim.heldItem = {"potion", "Invisibility Potion", 14, false};
  bystander.heldItem = {"potion", "Invisibility Potion", 14, false};
  require(monitor.observe({victim, bystander}, options, 1100).size() == 2,
          "first potion sighting did not alert for both players");
  require(monitor.observe({victim, bystander}, options, 1200).empty(),
          "unchanged potions re-alerted without a death in between");

  monitor.forgetPlayer("Steve");
  const auto afterDeath = monitor.observe({victim, bystander}, options, 1300);
  require(afterDeath.size() == 1 &&
              afterDeath[0].text.find("Steve") != std::string::npos,
          "forgetting the victim did not restore their alert, or leaked "
          "into the bystander who never died");

  monitor.forgetPlayer("Nobody Tracked");
  require(monitor.observe({victim, bystander}, options, 1400).empty(),
          "forgetting an untracked name disturbed unrelated players");
}

void deathVictimParsesKnownPhrasesAndRejectsProse() {
  require(parseDeathVictim("Steve was killed by Alex.") == "Steve",
          "killed-by phrasing did not extract the victim");
  require(parseDeathVictim("Steve was slain by Alex using a Diamond Sword.") ==
              "Steve",
          "slain-by phrasing did not extract the victim");
  require(parseDeathVictim("Steve_123 fell into the void.") == "Steve_123",
          "void death did not extract the victim, or mishandled the "
          "underscore");
  require(parseDeathVictim("Steve blew up.") == "Steve",
          "self-detonation phrasing did not extract the victim");
  require(parseDeathVictim(
              "Steve was killed by Alex and became a spooky ghost.") ==
              "Steve",
          "final-elimination suffix broke victim extraction");
  require(!parseDeathVictim("I heard Steve was killed by Alex yesterday"),
          "a sentence merely mentioning a death phrase was misread as a "
          "kill-feed line");
  require(!parseDeathVictim("Steve joined the lobby!"),
          "unrelated chat line was misread as a death");
  require(!parseDeathVictim(""), "empty message produced a victim");
}

void strictHeldItemAllowlistAndWoolRegressions() {
  const std::array<VisibleItem, 18> ignored = {{
      {"cloth", "Wool", 0, false},
      {"cloth", "Custom Named Wool", 7, false},
      {"cloth", "Wool", 14, true},
      {"clayHardenedStained", "Hardened Clay", 3, false},
      {"endStone", "End Stone", 0, false},
      {"wood", "Oak Planks", 0, false},
      {"stainedGlass", "Blast-Proof Glass", 11, false},
      {"obsidian", "Obsidian", 0, false},
      {"ladder", "Ladder", 0, false},
      {"waterBucket", "Water Bucket", 0, false},
      {"sponge", "Sponge", 0, false},
      {"ingotIron", "Iron Ingot", 0, false},
      {"ingotGold", "Gold Ingot", 0, false},
      {"diamond", "Diamond", 0, false},
      {"emerald", "Emerald", 0, false},
      {"pickaxeDiamond", "Diamond Pickaxe", 0, true},
      {"shears", "Shears", 0, false},
      {"compass", "Item Shop", 0, true},
  }};
  for (const auto &item : ignored) {
    require(classifyImportantItem(item) == ImportantItem::None &&
                readableItemName(item).empty(),
            "ordinary held item entered the explicit alert allowlist");
  }
  for (int metadata = 0; metadata < 16; ++metadata)
    require(classifyImportantItem(
                {"cloth", "Team Wool", metadata, metadata == 15}) ==
                ImportantItem::None,
            "a wool color entered the held-item allowlist");
  require(classifyImportantItem(
              {"mystery", "Diamond Sword", 0, true}) ==
              ImportantItem::None,
          "arbitrary display name promoted an unknown item");

  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  auto player = enemy(5, "Builder");
  monitor.observe({player}, options, 1000);
  Tick now = 1100;
  for (const auto &item : ignored) {
    player.heldItem = item;
    require(monitor.observe({player}, options, now).empty(),
            "ordinary building/resource/tool transition emitted an alert");
    now += 100;
  }

  player.heldItem = {"swordIron", "Iron Sword", 0, false};
  require(monitor.observe({player}, options, now).size() == 1,
          "relevant sword-tier increase did not alert");
  now += 100;
  player.heldItem = {"cloth", "Wool", 4, true};
  require(monitor.observe({player}, options, now).empty(),
          "wool alerted after a sword");
  now += 100;
  player.heldItem = {"potion", "Speed Potion", 2, false};
  require(monitor.observe({player}, options, now).size() == 1,
          "wool before a real potion suppressed the potion alert");
  for (int i = 0; i < 8; ++i) {
    now += 100;
    player.heldItem = i % 2 == 0
                          ? VisibleItem{"cloth", "Wool", i, false}
                          : VisibleItem{"potion", "Speed Potion", 2, false};
    require(monitor.observe({player}, options, now).empty(),
            "repeated wool/item slot switching spammed alerts");
  }
}

void matchLongItemStateHandlesRangeAndEntityReuse() {
  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  auto original = enemy(12, "Original");
  monitor.observe({original}, options, 1000);
  original.heldItem = {"bow", "Bow", 0, false};
  require(monitor.observe({original}, options, 1100).size() == 1,
          "first bow reveal did not alert");
  monitor.observe({}, options, 20000);
  require(monitor.observe({original}, options, 20100).empty(),
          "range exit and re-entry forgot a seen item");

  auto replacement = enemy(12, "Replacement");
  replacement.heldItem = {"bow", "Bow", 0, false};
  require(monitor.observe({replacement}, options, 20200).empty(),
          "new identity inherited the old entity's alert state");
  original.entityId = 44;
  require(monitor.observe({original}, options, 20300).empty(),
          "same identity with a new entity id forgot match-long state");
}

void swordTiersAreMonotonicAndAllowlistIsExplicit() {
  require(classifySwordTier({"swordWood", "", 0, false}) == SwordTier::Wood &&
              classifySwordTier({"swordStone", "", 0, false}) ==
                  SwordTier::Stone &&
              classifySwordTier({"swordIron", "", 0, false}) ==
                  SwordTier::Iron &&
              classifySwordTier({"swordDiamond", "", 0, false}) ==
                  SwordTier::Diamond,
          "one of the exact sword registry types was not recognized");
  PlayerMonitor monitor;
  auto options = rangeItemOptions();
  auto player = enemy(2, "Duelist");
  player.heldItem = {"swordWood", "Wooden Sword", 0, false};
  monitor.observe({player}, options, 1000);
  player.heldItem = {"swordStone", "Stone Sword", 0, false};
  require(monitor.observe({player}, options, 1100).empty(),
          "non-alert sword tier emitted an alert");
  player.heldItem = {"swordIron", "Iron Sword", 0, false};
  require(monitor.observe({player}, options, 1200).size() == 1,
          "Iron sword upgrade did not alert");
  player.heldItem = {"swordWood", "Wooden Sword", 0, false};
  monitor.observe({player}, options, 1300);
  player.heldItem = {"swordIron", "Iron Sword", 0, false};
  require(monitor.observe({player}, options, 1400).empty(),
          "sword downgrade reset the highest seen tier");
  player.heldItem = {"swordDiamond", "Diamond Sword", 0, false};
  require(monitor.observe({player}, options, 1500).size() == 1,
          "Diamond sword upgrade did not alert");

  const std::array<ImportantItem, 10> allowed = {
      ImportantItem::Bow, ImportantItem::Tnt, ImportantItem::Fireball,
      ImportantItem::EnderPearl, ImportantItem::GoldenApple,
      ImportantItem::Milk, ImportantItem::SpeedPotion,
      ImportantItem::JumpPotion, ImportantItem::InvisibilityPotion,
      ImportantItem::KnockbackStick};
  for (const auto item : allowed)
    require(std::string(importantItemName(item)).size() > 0,
            "an allowlisted item has no user-facing name");
}

void playerVisibilityAndIdentityRejectionsAreCounted() {
  PlayerMonitor monitor;
  PlayerAlertOptions options;
  options.items = true;
  options.maximumDistance = 256;
  options.visibility = VisibilityMode::LineOfSight;
  auto local = enemy(1, "local"); local.localPlayer = true;
  auto mate = enemy(2, "mate"); mate.teammateKnown = mate.teammate = true;
  auto dead = enemy(3, "dead"); dead.alive = false;
  auto spectator = enemy(4, "spectator"); spectator.spectator = true;
  auto far = enemy(5, "far"); far.distance = 257;
  auto wall = enemy(6, "wall"); wall.hasLineOfSight = false;
  auto unknown = enemy(7, "unknown"); unknown.lineOfSightKnown = false;
  auto invalid = enemy(-1, "");
  monitor.observe({local, mate, dead, spectator, far, wall, unknown, invalid},
                  options, 1000);
  const auto &counts = monitor.rejectionCounts();
  require(counts[static_cast<std::size_t>(PlayerRejectReason::LocalPlayer)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::Teammate)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::Dead)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::Spectator)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::OutOfRange)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::NoLineOfSight)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::UnknownVisibility)] == 1 &&
              counts[static_cast<std::size_t>(PlayerRejectReason::InvalidIdentity)] == 1,
          "player rejection diagnostics were incomplete");
  const auto &details = monitor.rejectionDetails();
  require(details.size() == 8 && details[5].identity == "wall" &&
              details[5].reason == PlayerRejectReason::NoLineOfSight &&
              details[7].reason == PlayerRejectReason::InvalidIdentity,
          "per-player rejection details were not retained safely");
}

void rangeAndCameraModesApplyTheirOwnRules() {
  auto player = enemy(1, "Steve");
  player.distance = 200.0;
  player.hasLineOfSight = false;
  PlayerMonitor rangeMonitor;
  auto range = rangeItemOptions();
  range.maximumDistance = 999.0;
  rangeMonitor.observe({player}, range, 1000);
  player.heldItem.typeName = "bow";
  require(rangeMonitor.observe({player}, range, 1100).size() == 1,
          "Range Only rejected a loaded player inside 256 blocks");
  player.distance = 256.01;
  require(rangeMonitor.observe({player}, range, 2000).empty(),
          "range configuration exceeded its bounded ceiling");

  PlayerMonitor cameraMonitor;
  PlayerAlertOptions camera = range;
  camera.visibility = VisibilityMode::CameraView;
  auto behind = enemy(2, "Behind");
  behind.insideCameraView = false;
  cameraMonitor.observe({behind}, camera, 1000);
  require(cameraMonitor.rejectionCounts()[static_cast<std::size_t>(
              PlayerRejectReason::OutsideCameraView)] == 1,
          "Camera View accepted a player behind the camera region");
  behind.insideCameraView = true;
  behind.hasLineOfSight = false;
  cameraMonitor.observe({behind}, camera, 1100);
  require(cameraMonitor.rejectionCounts()[static_cast<std::size_t>(
              PlayerRejectReason::NoLineOfSight)] == 1,
          "Camera View did not also require line of sight");
}

void notificationDurationsAndMaximumAreDeterministic() {
  NotificationTimeline timeline;
  timeline.add("one", 1000, 10, 2);
  require(timeline.records()[0].durationMs == 1000,
          "notification minimum duration was not clamped");
  timeline.add("two", 1001, 3000, 2);
  timeline.add("three", 1002, 30000, 2);
  require(timeline.size() == 2 && timeline.records().front().key == "two" &&
              timeline.records().back().durationMs == 15000,
          "notification maximum or visible bound failed");
  timeline.expire(4001);
  require(timeline.size() == 1 && timeline.records()[0].key == "three",
          "notification expiry boundary failed");
  timeline.expire(16002);
  require(timeline.size() == 0,
          "notification did not expire at exact duration");
}

OwnBed horizontalBed() {
  return {{10, 64, 10}, {11, 64, 10}, BedAxis::X, TeamId::Blue,
          true, true, "test", 7};
}

PlacementObservation validPlacementBase(const OwnBed &bed) {
  PlacementObservation observation;
  observation.masterEnabled = true;
  observation.moduleEnabled = true;
  observation.hookAvailable = true;
  observation.activeMatch = true;
  observation.obsidianHeld = true;
  observation.targetKnown = true;
  observation.worldCurrent = true;
  observation.localTeam = bed.team;
  observation.ownBed = bed;
  return observation;
}

void exactObsidianShellAllowsEightForBothAxes() {
  const std::array<OwnBed, 2> beds = {
      horizontalBed(),
      OwnBed{{20, 70, 20}, {20, 70, 21}, BedAxis::Z, TeamId::Pink,
             true, true, "test", 9}};
  for (const auto &bed : beds) {
    const auto shell = obsidianShell(bed);
    std::vector<BlockPosition> unique(shell.begin(), shell.end());
    std::sort(unique.begin(), unique.end(), [](const auto &a, const auto &b) {
      if (a.x != b.x) return a.x < b.x;
      if (a.y != b.y) return a.y < b.y;
      return a.z < b.z;
    });
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    require(unique.size() == 8, "bed shell did not contain exactly eight cells");
    for (const auto &position : shell) {
      require(isValidObsidianShellPosition(bed, position),
              "one valid shell cell was rejected");
      auto observation = validPlacementBase(bed);
      observation.resultingPosition = position;
      require(!evaluateObsidianPlacement(observation).cancel,
              "valid obsidian shell placement was cancelled");
      auto beside = validPlacementBase(bed);
      beside.resultingPosition = {position.x, position.y + 1, position.z};
      require(evaluateObsidianPlacement(beside).cancel,
              "invalid position immediately beside a shell cell was allowed");
    }
    auto invalid = validPlacementBase(bed);
    invalid.resultingPosition = {bed.head.x, bed.head.y - 1, bed.head.z};
    require(evaluateObsidianPlacement(invalid).cancel,
            "nearby invalid obsidian cell was allowed");
  }

  OwnBed reversed = horizontalBed();
  std::swap(reversed.head, reversed.foot);
  const auto forwardShell = obsidianShell(horizontalBed());
  const auto reverseShell = obsidianShell(reversed);
  for (const auto &position : forwardShell)
    require(std::find(reverseShell.begin(), reverseShell.end(), position) !=
                reverseShell.end(),
            "reversing head and foot changed the eight-cell shell");
}

void antiMisplaceFailsOpenForEveryUncertainSignal() {
  const OwnBed bed = horizontalBed();
  auto base = validPlacementBase(bed);
  base.resultingPosition = {50, 64, 50};
  require(evaluateObsidianPlacement(base).cancel,
          "fully confirmed invalid obsidian placement was not cancelled");
  const std::array<std::function<void(PlacementObservation &)>, 10> weaken = {
      [](auto &o) { o.masterEnabled = false; },
      [](auto &o) { o.moduleEnabled = false; },
      [](auto &o) { o.hookAvailable = false; },
      [](auto &o) { o.activeMatch = false; },
      [](auto &o) { o.obsidianHeld = false; },
      [](auto &o) { o.targetKnown = false; },
      [](auto &o) { o.worldCurrent = false; },
      [](auto &o) { o.localTeam = TeamId::Unknown; },
      [](auto &o) { o.ownBedDestroyed = true; },
      [](auto &o) { o.ownBed.reset(); }};
  for (const auto &change : weaken) {
    auto uncertain = base;
    change(uncertain);
    require(!evaluateObsidianPlacement(uncertain).cancel,
            "uncertain placement signal did not fail open");
  }
  auto wrongTeam = base;
  wrongTeam.localTeam = TeamId::Red;
  require(!evaluateObsidianPlacement(wrongTeam).cancel,
          "wrong-team bed did not fail open");
  auto malformedBed = base;
  malformedBed.ownBed->foot = {12, 64, 10};
  require(!isValidObsidianShellPosition(*malformedBed.ownBed,
                                        malformedBed.resultingPosition),
          "malformed two-block bed was accepted as shell geometry");
}

void antiMisplaceTargetFacesReadinessAndPropagation() {
  const BlockPosition target{10, 64, 10};
  require(resultingPlacementPosition(target, {1, 0, 0}, false) ==
              BlockPosition{11, 64, 10},
          "non-replaceable target did not apply the clicked face");
  require(resultingPlacementPosition(target, {0, 1, 0}, true) == target,
          "replaceable target incorrectly applied the clicked face");

  auto ready = validPlacementBase(horizontalBed());
  require(antiMisplaceStatus(ready) == AntiMisplaceStatus::Ready,
          "fully known placement state was not ready");
  auto state = ready;
  state.masterEnabled = false;
  require(antiMisplaceStatus(state) == AntiMisplaceStatus::Disabled,
          "master gate did not disable readiness");
  state = ready;
  state.activeMatch = false;
  require(antiMisplaceStatus(state) ==
              AntiMisplaceStatus::NotInActiveMatch,
          "inactive match did not explain readiness");
  state = ready;
  state.hookAvailable = false;
  require(antiMisplaceStatus(state) ==
              AntiMisplaceStatus::PlacementHookUnavailable,
          "hook failure did not explain readiness");
  state = ready;
  state.localTeam = TeamId::Unknown;
  require(antiMisplaceStatus(state) == AntiMisplaceStatus::WaitingForTeam,
          "unknown team did not explain readiness");
  state = ready;
  state.ownBed.reset();
  require(antiMisplaceStatus(state) == AntiMisplaceStatus::WaitingForOwnBed,
          "unknown bed did not explain readiness");
  state = ready;
  state.ownBedDestroyed = true;
  require(antiMisplaceStatus(state) == AntiMisplaceStatus::OwnBedDestroyed,
          "destroyed own bed did not disable readiness");

  ready.resultingPosition = {100, 70, 100};
  const auto cancel = evaluateObsidianPlacement(ready);
  require(cancellationPreventsPlacementAction(cancel, true) &&
              !cancellationPreventsPlacementAction(cancel, false),
          "cancellation was not tied to the pre-packet boundary");
  require(!controllerResultForCancelledPlacement() &&
              !callerFallbackForCancelledPlacement(),
          "cancelled placement would trigger a controller or caller action");
  for (int invocation = 0; invocation < 20; ++invocation)
    require(evaluateObsidianPlacement(ready).cancel,
            "repeated held-right-click invocation escaped cancellation");
  ready.obsidianHeld = false;
  require(!evaluateObsidianPlacement(ready).cancel,
          "normal non-obsidian right click was consumed");
}

void everyBuiltInMapHeightResolvesAndIsConsistent() {
  const auto &maps = builtInMapHeights();
  require(maps.size() == 192, "reviewed map table entry count changed");
  std::unordered_set<std::string> names;
  for (const auto &entry : maps) {
    const std::string normalized = normalizeMapName(entry.canonicalName);
    require(names.insert(normalized).second, "duplicate built-in map name");
    require(entry.maximumPlayerY == entry.maximumPlacementY + 1,
            "map player ceiling and placement ceiling are ambiguous");
    const auto resolved = resolveMapHeight(entry.canonicalName);
    require(resolved.maximumPlayerY == entry.maximumPlayerY &&
                resolved.maximumPlacementY == entry.maximumPlacementY &&
                !resolved.overridden,
            "built-in map entry failed to resolve");
  }
}

void mapAliasesFormattingUnknownAndOverrideWork() {
  const auto sky = resolveMapHeight("  \xC2\xA7" "bSky-Rise  ");
  require(sky.canonicalName == "Sky Rise" && sky.maximumPlacementY == 90,
          "formatted map alias did not normalize");
  const auto bio = resolveMapHeight("BIO HAZARD");
  require(bio.canonicalName == "Bio-Hazard" && bio.maximumPlacementY == 95,
          "hyphenated map alias failed");
  const auto parsed = parseMapScoreboardLine("\xC2\xA7" "aMap:  Trick-or-Yeet ");
  require(parsed && *parsed == "trick or yeet",
          "formatted scoreboard Map line did not parse");
  require(!parseMapScoreboardLine("Mode: Solo") &&
              !parseMapScoreboardLine("Map:"),
          "non-map or empty map line parsed");
  require(!resolveMapHeight("Future Unknown Map").maximumPlacementY,
          "unknown map received a guessed height");
  const auto overridden = resolveMapHeight(
      "Future-Unknown Map", {{"future unknown map", 123}});
  require(overridden.overridden && overridden.maximumPlacementY == 123 &&
              overridden.maximumPlayerY == 124,
          "manual per-map override did not take precedence");
  require(!resolveMapHeight("Future Unknown Map",
                            {{"future unknown map", 999}})
               .maximumPlacementY,
          "malformed manual override was accepted");
}

void heightBoundariesUsePlacementCeiling() {
  const auto below = evaluateHeight(99.99, 100);
  const auto at = evaluateHeight(100.0, 100);
  const auto above = evaluateHeight(101.0, 100);
  require(below.currentY == 99 && below.remaining == 1 &&
              at.remaining == 0 && above.remaining == 0,
          "height placement boundary calculation failed");
}

void hudDefaultsAreIndependentAndRecoverable() {
  const Configuration::Settings defaults;
  std::unordered_set<std::string> positions;
  for (std::size_t i = 0; i < kHudCount; ++i) {
    const auto &hud = defaults.hud[i];
    require(!hud.visible, "master defaults forced a HUD visible");
    positions.insert(std::to_string(hud.x) + ":" + std::to_string(hud.y));
  }
  require(positions.size() == kHudCount,
          "default HUD positions overlap exactly");
  HudLayout malformed{true, 2.0F, -1.0F,
                      std::numeric_limits<float>::infinity()};
  const auto safe = sanitizeHudLayout(malformed, 0.25F, 0.20F);
  require(safe.visible && safe.x == 0.75F && safe.y == 0.0F &&
              safe.scale == 1.0F,
          "HUD screen-bound recovery failed");
}

void configV2PersistsHudDurationsOverridesAndMaster() {
  Configuration::Settings settings;
  settings.masterEnabled = true;
  settings.modules[static_cast<std::size_t>(Module::ItemAlerts)] = true;
  settings.visibilityMode = VisibilityMode::CameraView;
  settings.cameraViewDegrees = 123.0F;
  settings.defaultNotificationSeconds = 2.0F;
  settings.importantNotificationSeconds = 6.0F;
  settings.playerNotificationSeconds = 7.0F;
  settings.warningNotificationSeconds = 8.0F;
  settings.maximumVisibleNotifications = 4;
  settings.hud[static_cast<std::size_t>(HudId::BedStatus)] =
      {true, 0.31F, 0.41F, 1.6F};
  settings.mapPlacementOverrides["Future Map"] = 122;
  const auto decoded = Configuration::deserialize(
      Configuration::serialize(settings));
  const auto &hud = decoded.hud[static_cast<std::size_t>(HudId::BedStatus)];
  require(decoded.formatVersion == 3 && decoded.masterEnabled &&
              decoded.modules[static_cast<std::size_t>(Module::ItemAlerts)] &&
              decoded.visibilityMode == VisibilityMode::CameraView &&
              decoded.cameraViewDegrees == 123.0F && hud.visible &&
              std::abs(hud.x - 0.31F) < 0.001F &&
              std::abs(hud.scale - 1.6F) < 0.001F &&
              decoded.maximumVisibleNotifications == 4 &&
              decoded.mapPlacementOverrides.at("future map") == 122,
          "v2 configuration round trip lost new settings");
}

void configV1MigrationPreservesPreferencesAndPositions() {
  const auto migrated = Configuration::deserialize(
      "master=1;eventTimers=1;heightOverlay=1;resourceTracker=1;"
      "resourceHud=1;upgradeHud=1;bedTracker=1;timerX=0.25;timerY=0.35;"
      "timerScale=1.5;heightX=0.45;heightY=0.55;heightScale=1.7;");
  require(migrated.formatVersion == 3 && migrated.masterEnabled &&
              migrated.visibilityMode == VisibilityMode::RangeOnly,
          "v1 compatibility defaults failed");
  const auto timer = migrated.hud[static_cast<std::size_t>(HudId::EventTimer)];
  const auto height = migrated.hud[static_cast<std::size_t>(HudId::Height)];
  require(timer.visible && std::abs(timer.x - 0.25F) < 0.001F &&
              std::abs(timer.scale - 1.5F) < 0.001F && height.visible &&
              std::abs(height.y - 0.55F) < 0.001F &&
              migrated.hud[static_cast<std::size_t>(HudId::Resource)].visible &&
              migrated.hud[static_cast<std::size_t>(HudId::TeamState)].visible &&
              migrated.hud[static_cast<std::size_t>(HudId::BedDistance)].visible,
          "v1 module and HUD preferences were not migrated");
}

void configMalformedNewFieldsAreIndependentlySanitized() {
  const auto settings = Configuration::deserialize(
      "version=2;master=1;visibilityMode=99;cameraViewDegrees=nan;"
      "notificationDefault=0;notificationImportant=99;notificationPlayer=inf;"
      "notificationWarning=5junk;notificationMaximum=999;hud0Visible=1;"
      "hud0X=nan;hud0Y=2;hud0Scale=-9;mapOverrides=good:120|bad:12junk|huge:999;");
  require(settings.masterEnabled &&
              settings.visibilityMode == VisibilityMode::CameraView &&
              settings.cameraViewDegrees == 100.0F &&
              settings.defaultNotificationSeconds == 1.0F &&
              settings.importantNotificationSeconds == 15.0F &&
              settings.playerNotificationSeconds == 4.0F &&
              settings.warningNotificationSeconds == 5.0F &&
              settings.maximumVisibleNotifications == 10,
          "malformed new numeric fields were not safely isolated");
  const auto hud = settings.hud[0];
  require(hud.visible && hud.x == 0.02F && hud.y == 1.0F && hud.scale == 0.5F,
          "malformed HUD fields erased unrelated settings");
  require(settings.mapPlacementOverrides.size() == 1 &&
              settings.mapPlacementOverrides.at("good") == 120,
          "malformed map overrides contaminated valid entries");
}

void removedRendererConfigKeysAreIgnoredWithoutModuleShifts() {
  for (const char *version : {"1", "2"}) {
    for (const char *oldToggle : {"0", "1"}) {
      const std::string data =
          std::string("version=") + version + ";master=1;eventTimers=1;" +
          "bedEsp=" + oldToggle +
          ";bedEspOpacity=0.75;bedEspRed=1;bedEspGreen=2;bedEspBlue=3;"
          "unknownBedRed=4;unknownBedGreen=5;unknownBedBlue=6;"
          "pickupAlerts=1;armorAlerts=1;trapNotifier=1;resourceTracker=1;"
          "itemAlerts=1;upgradeHud=1;antiMisplace=1;";
      const auto migrated = Configuration::deserialize(data);
      require(migrated.formatVersion == 3 &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::PickupAlerts)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::ArmorAlerts)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::TrapNotifier)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::ResourceTracker)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::ItemAlerts)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::UpgradeHud)] &&
                  migrated.modules[static_cast<std::size_t>(
                      Module::AntiMisplace)],
              "old renderer key shifted a remaining named module");
      const std::string encoded = Configuration::serialize(migrated);
      require(encoded.find("bedEsp") == std::string::npos &&
                  encoded.find("unknownBed") == std::string::npos,
              "ignored old renderer keys were resurrected");
    }
  }
}

void configurationSettersPersistMasterAndSavedChildren() {
  Config::g_testBedwarsSettings = "version=2;master=0;itemAlerts=0;";
  Configuration::reload();
  Configuration::setModuleEnabled(Module::ItemAlerts, true);
  require(!Configuration::get().enabled(Module::ItemAlerts) &&
              Configuration::isModuleEnabled(Module::ItemAlerts),
          "saved child preference became active with master off");
  Configuration::setMasterEnabled(true);
  require(Configuration::get().enabled(Module::ItemAlerts),
          "master did not activate the saved child preference");
  const auto persisted = Configuration::deserialize(Config::g_testBedwarsSettings);
  require(persisted.masterEnabled &&
              persisted.modules[static_cast<std::size_t>(Module::ItemAlerts)],
          "master or child preference was not persisted");
}

void shutdownCoordinatorIsIdempotentAndMonotonic() {
  ShutdownCoordinator shutdown;
  require(!shutdown.stopping() && shutdown.request() && shutdown.stopping(),
          "first shutdown request failed");
  require(!shutdown.request(), "second shutdown request was not idempotent");
  require(shutdown.advance(ShutdownCoordinator::Stage::ScanStopped),
          "shutdown stage did not advance");
  require(!shutdown.advance(ShutdownCoordinator::Stage::EventLoopExited) &&
              shutdown.stage() == ShutdownCoordinator::Stage::ScanStopped,
          "shutdown stage regressed");
  shutdown.advance(ShutdownCoordinator::Stage::Complete);
  require(std::string(shutdownStageName(shutdown.stage())) == "complete",
          "shutdown completion stage was not observable");
  shutdown.resetForTests();
  require(!shutdown.stopping(), "test reset did not restore running state");
}

void boundedQueueDropsOnlyOldestAndReportsDrops() {
  BoundedQueue<int, 3> queue;
  require(!queue.push(1) && !queue.push(2) && !queue.push(3),
          "queue dropped before reaching its bound");
  require(queue.push(4) && queue.size() == 3,
          "queue did not enforce its fixed bound");
  const auto values = queue.take();
  require(values.size() == 3 && values[0] == 2 && values[2] == 4,
          "queue did not retain the newest bounded values");
  require(queue.consumeDropped() == 1 && queue.consumeDropped() == 0,
          "queue drop count was not consumed deterministically");
}

void bedScanPublicationRejectsCancelledAndStaleWorkers() {
  require(canPublishBedScanResult(3, 3, true, false),
          "current enabled scan could not publish");
  require(!canPublishBedScanResult(2, 3, true, false) &&
              !canPublishBedScanResult(3, 3, false, false) &&
              !canPublishBedScanResult(3, 3, true, true) &&
              !canPublishBedScanResult(0, 0, true, false),
          "stale, disabled, cancelled, or uninitialized scan could publish");
}

} // namespace

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"lifecycle starts and elapsed", lifecycleStartsAndTracksElapsed},
      {"lifecycle late injection", lifecycleLateInjectionDoesNotInventClock},
      {"lifecycle pre-game", lifecyclePreGameStartsOnExit},
      {"lifecycle unknown mode", lifecycleRejectsUnknownMode},
      {"lifecycle world reset", lifecycleWorldChangeResets},
      {"lifecycle clock regression", lifecycleClockRegressionResets},
      {"lifecycle disable reconnect", lifecycleDisableAndReconnect},
      {"lifecycle player death respawn",
       lifecyclePlayerLossDeathAndRespawnPreserveGameClock},
      {"lifecycle game end", lifecycleLeavingGameEndsAndClearsClock},
      {"timer boundary", timerBoundaryBeforeAndAtEvent},
      {"timer extreme", timerNeverGoesNegative},
      {"timer scoreboard parse", timerParsesScoreboard},
      {"resources initial", resourcesIgnoreInitialSnapshot},
      {"resources additions", resourcesReportAdditionsOnly},
      {"resources transfer", resourcesIgnoreContainerTransfer},
      {"resources unknown container", resourcesIgnoreUnknownContainerMovement},
      {"resources reset", resourcesResetDropsHistory},
      {"upgrade parse dedupe", upgradeParsingAndDeduplication},
      {"trap parse reset", trapParsingAndReset},
      {"player initial", playerMonitorIgnoresInitialEquipment},
      {"player armor", playerMonitorArmorTransition},
      {"player filters", playerMonitorTeamAndRangeFiltering},
      {"player entity reuse", playerMonitorEntityReuseDoesNotAlert},
      {"player cooldown", playerMonitorCooldownAndLaterAlert},
      {"player burst capacity", playerMonitorPrunesAndCapsBursts},
      {"bed distance", bedDistanceUnknownAndBoundary},
      {"anti misplace rule", antiMisplaceRequiresExactKnownPosition},
      {"shop affordable", shopParsesAffordableVisibleLore},
      {"shop malformed", shopRejectsMalformedAndUnknownCurrency},
      {"shop duplicate", shopUnformattedCodesAndDuplicateProtection},
      {"height", heightKnownAndUnknown},
      {"invalid floating point", invalidFloatingPointInputsAreSafe},
      {"config defaults", configDefaultsAreDisabled},
      {"config unavailable modules", configKeepsUnavailableModulesDisabled},
      {"config round trip", configRoundTripPreservesNamedSettings},
      {"config malformed", configRejectsMalformedAndClampsRanges},
      {"eight team normalization", allEightTeamsNormalizeAndColorDistinctly},
      {"team authority reuse expiry", teamTrackerHandlesAuthorityReuseExpiryAndReset},
      {"team capacity and beds", teamTrackerIsBoundedAndTracksAllBedStates},
      {"bed destruction parsing", bedDestructionParsesAllTeamsConservatively},
      {"player alert identities", everyPlayerAlertContainsColoredIdentity},
      {"team Sharpness observation", enchantedSwordProducesOneTeamSharpnessOnly},
      {"knockback stick", knockbackStickClassificationAndDeduplication},
      {"potions", potionClassificationDedupAndTransition},
      {"death forgets tracked player only", deathForgetsOnlyThatPlayersAlertHistory},
      {"death victim parsing", deathVictimParsesKnownPhrasesAndRejectsProse},
      {"strict held-item allowlist", strictHeldItemAllowlistAndWoolRegressions},
      {"match-long item state", matchLongItemStateHandlesRangeAndEntityReuse},
      {"monotonic sword tiers", swordTiersAreMonotonicAndAllowlistIsExplicit},
      {"player rejection reasons", playerVisibilityAndIdentityRejectionsAreCounted},
      {"range and camera modes", rangeAndCameraModesApplyTheirOwnRules},
      {"notification timeline", notificationDurationsAndMaximumAreDeterministic},
      {"obsidian shells", exactObsidianShellAllowsEightForBothAxes},
      {"anti misplace fail open", antiMisplaceFailsOpenForEveryUncertainSignal},
      {"anti misplace boundary and status",
       antiMisplaceTargetFacesReadinessAndPropagation},
      {"all built-in maps", everyBuiltInMapHeightResolvesAndIsConsistent},
      {"map aliases overrides", mapAliasesFormattingUnknownAndOverrideWork},
      {"height boundaries", heightBoundariesUsePlacementCeiling},
      {"HUD defaults recovery", hudDefaultsAreIndependentAndRecoverable},
      {"config v2 new settings", configV2PersistsHudDurationsOverridesAndMaster},
      {"config v1 migration", configV1MigrationPreservesPreferencesAndPositions},
      {"config malformed new fields", configMalformedNewFieldsAreIndependentlySanitized},
      {"removed renderer config migration",
       removedRendererConfigKeysAreIgnoredWithoutModuleShifts},
      {"config master persistence", configurationSettersPersistMasterAndSavedChildren},
      {"shutdown coordinator", shutdownCoordinatorIsIdempotentAndMonotonic},
      {"bounded queue", boundedQueueDropsOnlyOldestAndReportsDrops},
      {"bed scan publication", bedScanPublicationRejectsCancelledAndStaleWorkers},
  };

  int failures = 0;
  for (const auto &[name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() - static_cast<std::size_t>(failures) << "/"
            << tests.size() << " passed\n";
  return failures == 0 ? 0 : 1;
}
