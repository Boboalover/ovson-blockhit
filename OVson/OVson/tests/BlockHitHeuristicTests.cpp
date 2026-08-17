#include "../Logic/BlockHitHeuristic.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace BlockHitHeuristic;

namespace {

#define REQUIRE(condition)                                                       \
  do {                                                                           \
    if (!(condition))                                                            \
      throw std::runtime_error(std::string("requirement failed: ") + #condition); \
  } while (false)

bool hasCode(const Result &result, DiagnosticCode code) {
  for (std::size_t i = 0; i < result.diagnosticCount; ++i)
    if (result.diagnostics[i].code == code) return true;
  return false;
}

Detector enabledDetector(Millis atMs = 0) {
  Detector detector;
  REQUIRE(!detector.setEnabled(true, atMs).playSound);
  return detector;
}

SwingEvent swing(Millis atMs, double distance = 3.0, int entityId = 2) {
  return {atMs, entityId, distance, true, false, true, false};
}

HurtEvent hurt(Millis atMs, Hazard hazards = Hazard::None) {
  return {atMs, true, true, true, hazards};
}

void test_health_confirmed_hit() {
  auto detector = enabledDetector();
  detector.observeHealth(10, 20.0f);
  detector.observeSwing(swing(100));
  detector.observeHurt(hurt(110));
  const Result result = detector.observeHealth(120, 18.0f);
  REQUIRE(result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HealthConfirmationMatched));
  REQUIRE(hasCode(result, DiagnosticCode::SoundTriggered));
}

void test_velocity_confirmed_hit() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeHurt(hurt(110));
  const Result result = detector.observeVelocity(120, true, 100, 20, -50);
  REQUIRE(result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::VelocityConfirmationMatched));
}

void test_confirmation_before_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeVelocity(105, true, 1, 0, 0);
  const Result result = detector.observeHurt(hurt(110));
  REQUIRE(result.playSound);
}

void test_health_before_hurt() {
  auto detector = enabledDetector();
  detector.observeHealth(10, 20.0f);
  detector.observeSwing(swing(100));
  detector.observeHealth(105, 19.0f);
  REQUIRE(detector.observeHurt(hurt(110)).playSound);
}

void test_confirmation_after_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeHurt(hurt(110));
  REQUIRE(detector.observeVelocity(250, true, 1, 0, 0).playSound);
}

void test_swing_after_hurt() {
  auto detector = enabledDetector();
  detector.observeHurt(hurt(100));
  detector.observeVelocity(120, true, 1, 0, 0);
  REQUIRE(detector.observeSwing(swing(180)).playSound);
}

void test_absorption_fallback() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0));
  detector.observeHurt(hurt(110));
  REQUIRE(!detector.advance(299).playSound);
  const Result result = detector.advance(300);
  REQUIRE(result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::FallbackUsed));
}

void test_two_legitimate_hits() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0, 2));
  detector.observeHurt(hurt(110));
  REQUIRE(detector.observeVelocity(120, true, 1, 0, 0).playSound);
  detector.observeSwing(swing(500, 3.0, 2));
  detector.observeHurt(hurt(510));
  REQUIRE(detector.observeVelocity(520, true, 1, 0, 0).playSound);
}

void test_attacker_movement_prefers_closest_swing() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(90, 4.8, 2));
  detector.observeSwing(swing(100, 3.2, 2));
  detector.observeVelocity(105, true, 1, 0, 0);
  const Result result = detector.observeHurt(hurt(110));
  REQUIRE(result.playSound);
  bool found = false;
  for (std::size_t i = 0; i < result.diagnosticCount; ++i) {
    if (result.diagnostics[i].code == DiagnosticCode::SoundTriggered) {
      REQUIRE(std::abs(result.diagnostics[i].distance - 3.2) < 0.0001);
      found = true;
    }
  }
  REQUIRE(found);
}

void test_unknown_team_with_strong_evidence() {
  auto detector = enabledDetector();
  auto event = swing(100);
  event.teamKnown = false;
  detector.observeSwing(event);
  detector.observeHurt(hurt(110));
  REQUIRE(detector.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_blocking_without_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  REQUIRE(!detector.advance(1000).playSound);
}

void test_hurt_without_blocking() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  HurtEvent event = hurt(110);
  event.blocking = false;
  const Result result = detector.observeHurt(event);
  REQUIRE(!result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HurtRejectedNotBlocking));
}

void test_blocking_with_non_sword() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  HurtEvent event = hurt(110);
  event.holdingSword = false;
  const Result result = detector.observeHurt(event);
  REQUIRE(!result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HurtRejectedNotSword));
}

void test_swing_without_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeVelocity(110, true, 1, 0, 0);
  REQUIRE(!detector.advance(1000).playSound);
}

void test_health_without_swing() {
  auto detector = enabledDetector();
  detector.observeHealth(10, 20.0f);
  detector.observeHurt(hurt(100));
  detector.observeHealth(110, 18.0f);
  REQUIRE(!detector.advance(360).playSound);
}

void test_velocity_without_swing() {
  auto detector = enabledDetector();
  detector.observeHurt(hurt(100));
  detector.observeVelocity(110, true, 1, 0, 0);
  REQUIRE(!detector.advance(360).playSound);
}

void test_swing_outside_time_window() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeHurt(hurt(601));
  detector.observeVelocity(610, true, 1, 0, 0);
  REQUIRE(!detector.advance(861).playSound);
}

void test_swing_outside_range() {
  auto detector = enabledDetector();
  const Result swingResult = detector.observeSwing(swing(100, 5.001));
  REQUIRE(hasCode(swingResult, DiagnosticCode::CandidateRejectedRange));
  detector.observeHurt(hurt(110));
  REQUIRE(!detector.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_same_team_swing() {
  auto detector = enabledDetector();
  auto event = swing(100);
  event.sameTeam = true;
  const Result result = detector.observeSwing(event);
  REQUIRE(hasCode(result, DiagnosticCode::CandidateRejectedSameTeam));
  detector.observeHurt(hurt(110));
  REQUIRE(!detector.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_local_player_swing() {
  auto detector = enabledDetector();
  auto event = swing(100);
  event.isLocalPlayer = true;
  REQUIRE(hasCode(detector.observeSwing(event),
                  DiagnosticCode::CandidateRejectedLocalPlayer));
}

void test_non_player_swing() {
  auto detector = enabledDetector();
  auto event = swing(100);
  event.isPlayer = false;
  REQUIRE(hasCode(detector.observeSwing(event),
                  DiagnosticCode::CandidateRejectedNotPlayer));
}

void test_hurt_other_entity() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  HurtEvent event = hurt(110);
  event.targetsLocalPlayer = false;
  REQUIRE(hasCode(detector.observeHurt(event),
                  DiagnosticCode::HurtIgnoredDifferentEntity));
}

void test_arrow_like_damage_without_swing() {
  auto detector = enabledDetector();
  detector.observeHurt(hurt(100));
  detector.observeHealth(110, 18.0f);
  detector.observeVelocity(120, true, 1, 0, 0);
  REQUIRE(!detector.advance(360).playSound);
}

void expect_strong_hazard_rejection(Hazard hazard) {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  const Result result = detector.observeHurt(hurt(110, hazard));
  REQUIRE(!result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HurtRejectedEnvironmental));
}

void test_fall_damage() { expect_strong_hazard_rejection(Hazard::Fall); }
void test_suffocation_damage() {
  expect_strong_hazard_rejection(Hazard::Suffocation);
}
void test_void_damage() { expect_strong_hazard_rejection(Hazard::Void); }

void expect_periodic_hazard_rejection(Hazard hazard) {
  auto detector = enabledDetector();
  detector.observeHealth(10, 20.0f);
  detector.observeSwing(swing(100, 3.0));
  detector.observeHurt(hurt(110, hazard));
  detector.observeHealth(120, 19.0f);
  const Result result = detector.advance(370);
  REQUIRE(!result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HurtRejectedEnvironmental));
}

void test_fire_damage() { expect_periodic_hazard_rejection(Hazard::Fire); }
void test_lava_damage() { expect_periodic_hazard_rejection(Hazard::Lava); }
void test_drowning_damage() {
  expect_periodic_hazard_rejection(Hazard::Drowning);
}

void test_melee_while_burning_requires_velocity() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0));
  detector.observeHurt(hurt(110, Hazard::Fire));
  REQUIRE(detector.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_explosion_before_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeExplosion(105);
  REQUIRE(!detector.observeHurt(hurt(110)).playSound);
}

void test_explosion_after_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.observeHurt(hurt(110));
  const Result result = detector.observeExplosion(120);
  REQUIRE(!result.playSound);
  REQUIRE(hasCode(result, DiagnosticCode::HurtRejectedEnvironmental));
}

void test_explosion_veto_boundaries() {
  const auto rejectsBefore = [](Millis explosionAt) {
    auto detector = enabledDetector();
    detector.observeSwing(swing(500));
    detector.observeExplosion(explosionAt);
    return hasCode(detector.observeHurt(hurt(1000)),
                   DiagnosticCode::HurtRejectedEnvironmental);
  };
  REQUIRE(rejectsBefore(750));  // Exact 250 ms boundary.
  REQUIRE(rejectsBefore(751));  // One millisecond inside.
  REQUIRE(!rejectsBefore(749)); // One millisecond outside.

  const auto rejectsAfter = [](Millis explosionAt) {
    auto detector = enabledDetector();
    detector.observeSwing(swing(900));
    detector.observeHurt(hurt(1000));
    return hasCode(detector.observeExplosion(explosionAt),
                   DiagnosticCode::HurtRejectedEnvironmental);
  };
  REQUIRE(rejectsAfter(1250));  // Exact 250 ms boundary.
  REQUIRE(rejectsAfter(1249));  // One millisecond inside.
  REQUIRE(!rejectsAfter(1251)); // One millisecond outside.
}

void expect_reset_clears(ResetReason reason) {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.reset(reason, 105);
  detector.observeHurt(hurt(110));
  REQUIRE(!detector.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_world_reset() { expect_reset_clears(ResetReason::WorldChanged); }
void test_respawn_reset() { expect_reset_clears(ResetReason::Respawn); }
void test_disconnect_reset() { expect_reset_clears(ResetReason::Disconnect); }
void test_entity_replacement_reset() {
  expect_reset_clears(ResetReason::LocalEntityChanged);
}
void test_local_state_unavailable_reset() {
  expect_reset_clears(ResetReason::LocalStateUnavailable);
}

void test_disable_reenable_clears() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100));
  detector.setEnabled(false, 105);
  detector.setEnabled(true, 110);
  detector.observeHurt(hurt(120));
  REQUIRE(!detector.observeVelocity(130, true, 1, 0, 0).playSound);
}

void test_duplicate_hurt_one_sound() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0, 2));
  detector.observeHurt(hurt(110));
  REQUIRE(detector.observeVelocity(120, true, 1, 0, 0).playSound);
  detector.observeSwing(swing(200, 3.0, 2));
  const Result duplicate = detector.observeHurt(hurt(250));
  REQUIRE(!duplicate.playSound);
  REQUIRE(hasCode(duplicate, DiagnosticCode::DuplicateHurtSuppressed));
}

void test_one_swing_not_reused() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0));
  detector.observeHurt(hurt(110));
  REQUIRE(detector.advance(300).playSound);
  detector.observeHurt(hurt(500));
  detector.observeVelocity(510, true, 1, 0, 0);
  REQUIRE(!detector.advance(760).playSound);
}

void test_multiple_swings_without_hurt() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 2.5, 2));
  detector.observeSwing(swing(100, 3.0, 3));
  detector.observeSwing(swing(100, 4.0, 4));
  REQUIRE(!detector.advance(1000).playSound);
}

void test_nearby_swing_with_environmental_damage() {
  expect_strong_hazard_rejection(Hazard::Fall | Hazard::Suffocation);
}

void test_debug_independence() {
  auto first = enabledDetector();
  auto second = enabledDetector();
  first.observeSwing(swing(100));
  second.observeSwing(swing(100));
  first.observeHurt(hurt(110));
  second.observeHurt(hurt(110));
  REQUIRE(first.observeVelocity(120, true, 1, 0, 0).playSound ==
          second.observeVelocity(120, true, 1, 0, 0).playSound);
}

void test_swing_before_boundaries() {
  auto oneInside = enabledDetector();
  oneInside.observeSwing(swing(101));
  oneInside.observeVelocity(590, true, 1, 0, 0);
  REQUIRE(oneInside.observeHurt(hurt(600)).playSound);

  auto inside = enabledDetector();
  inside.observeSwing(swing(100));
  inside.observeVelocity(590, true, 1, 0, 0);
  REQUIRE(inside.observeHurt(hurt(600)).playSound);

  auto outside = enabledDetector();
  outside.observeSwing(swing(100));
  outside.observeVelocity(591, true, 1, 0, 0);
  REQUIRE(!outside.observeHurt(hurt(601)).playSound);
}

void test_swing_after_boundaries() {
  auto oneInside = enabledDetector();
  oneInside.observeHurt(hurt(100));
  oneInside.observeVelocity(110, true, 1, 0, 0);
  REQUIRE(oneInside.observeSwing(swing(279)).playSound);

  auto inside = enabledDetector();
  inside.observeHurt(hurt(100));
  inside.observeVelocity(110, true, 1, 0, 0);
  REQUIRE(inside.observeSwing(swing(280)).playSound);

  auto outside = enabledDetector();
  outside.observeHurt(hurt(100));
  outside.observeVelocity(110, true, 1, 0, 0);
  REQUIRE(!outside.observeSwing(swing(281)).playSound);
}

void test_range_boundaries() {
  auto confirmed = enabledDetector();
  confirmed.observeSwing(swing(100, 5.0));
  confirmed.observeHurt(hurt(110));
  REQUIRE(confirmed.observeVelocity(120, true, 1, 0, 0).playSound);

  auto fallbackInside = enabledDetector();
  fallbackInside.observeSwing(swing(100, 3.75));
  fallbackInside.observeHurt(hurt(110));
  REQUIRE(fallbackInside.advance(300).playSound);

  auto fallbackOutside = enabledDetector();
  fallbackOutside.observeSwing(swing(100, 3.7501));
  fallbackOutside.observeHurt(hurt(110));
  REQUIRE(!fallbackOutside.advance(300).playSound);
}

void test_confirmation_before_boundaries() {
  auto oneInside = enabledDetector();
  oneInside.observeSwing(swing(100));
  oneInside.observeVelocity(201, true, 1, 0, 0);
  REQUIRE(oneInside.observeHurt(hurt(340)).playSound);

  auto inside = enabledDetector();
  inside.observeSwing(swing(100));
  inside.observeVelocity(200, true, 1, 0, 0);
  REQUIRE(inside.observeHurt(hurt(340)).playSound);

  auto outside = enabledDetector();
  outside.observeSwing(swing(100));
  outside.observeVelocity(200, true, 1, 0, 0);
  REQUIRE(!outside.observeHurt(hurt(341)).playSound);
}

void test_confirmation_after_boundaries() {
  auto oneInside = enabledDetector();
  oneInside.observeSwing(swing(100, 4.0));
  oneInside.observeHurt(hurt(110));
  REQUIRE(oneInside.observeVelocity(369, true, 1, 0, 0).playSound);

  auto inside = enabledDetector();
  inside.observeSwing(swing(100, 4.0));
  inside.observeHurt(hurt(110));
  REQUIRE(inside.observeVelocity(370, true, 1, 0, 0).playSound);

  auto outside = enabledDetector();
  outside.observeSwing(swing(100, 4.0));
  outside.observeHurt(hurt(110));
  REQUIRE(!outside.observeVelocity(371, true, 1, 0, 0).playSound);
}

void test_fallback_wait_boundary() {
  auto detector = enabledDetector();
  detector.observeSwing(swing(100, 3.0));
  detector.observeHurt(hurt(110));
  REQUIRE(!detector.advance(299).playSound);
  REQUIRE(detector.advance(300).playSound);
}

void test_duplicate_boundary() {
  auto detector = enabledDetector();
  detector.observeHurt(hurt(100));
  REQUIRE(hasCode(detector.observeHurt(hurt(349)),
                  DiagnosticCode::DuplicateHurtSuppressed));
  REQUIRE(!hasCode(detector.observeHurt(hurt(350)),
                   DiagnosticCode::DuplicateHurtSuppressed));
}

void test_debounce_boundary() {
  auto inside = enabledDetector();
  inside.observeSwing(swing(100, 3.0, 2));
  inside.observeHurt(hurt(110));
  REQUIRE(inside.observeVelocity(120, true, 1, 0, 0).playSound);
  inside.observeSwing(swing(449, 3.0, 2));
  inside.observeHurt(hurt(459));
  const Result suppressed = inside.observeVelocity(460, true, 1, 0, 0);
  REQUIRE(!suppressed.playSound);
  REQUIRE(hasCode(suppressed, DiagnosticCode::DebounceSuppressed));

  auto boundary = enabledDetector();
  boundary.observeSwing(swing(100, 3.0, 2));
  boundary.observeHurt(hurt(110));
  REQUIRE(boundary.observeVelocity(120, true, 1, 0, 0).playSound);
  boundary.observeSwing(swing(450, 3.0, 2));
  boundary.observeHurt(hurt(460));
  REQUIRE(boundary.observeVelocity(461, true, 1, 0, 0).playSound);
}

void test_invalid_and_regressing_timestamps() {
  auto detector = enabledDetector();
  REQUIRE(hasCode(detector.observeSwing(swing(-1)),
                  DiagnosticCode::InvalidTimestamp));
  detector.observeSwing(swing(100));
  REQUIRE(hasCode(detector.observeHurt(hurt(99)),
                  DiagnosticCode::ClockRegression));
  REQUIRE(!detector.hasPendingHurt());
}

void test_extremely_large_timestamps() {
  const Millis maximum = std::numeric_limits<Millis>::max();
  auto detector = enabledDetector(maximum - 1000);
  detector.observeSwing(swing(maximum - 500, 3.0));
  detector.observeHurt(hurt(maximum - 400));
  REQUIRE(detector.observeVelocity(maximum - 300, true, 1, 0, 0).playSound);
}

void test_invalid_health() {
  auto detector = enabledDetector();
  REQUIRE(hasCode(detector.observeHealth(
                      10, std::numeric_limits<float>::quiet_NaN()),
                  DiagnosticCode::InvalidHealth));
}

void test_rapid_bursts_are_bounded() {
  auto detector = enabledDetector();
  for (int i = 0; i < 1000; ++i)
    detector.observeSwing(swing(100, 3.0, i + 2));
  REQUIRE(detector.storedSwingCount() <= Rules::MaximumSwings);

  detector.observeHealth(100, 20.0f);
  for (int i = 0; i < 1000; ++i) {
    detector.observeHealth(100, (i % 2 == 0) ? 19.0f : 20.0f);
    detector.observeVelocity(100, true, 1, 0, 0);
  }
  REQUIRE(detector.storedConfirmationCount() <=
          Rules::MaximumConfirmations * 2);
}

using Test = std::pair<const char *, std::function<void()>>;

} // namespace

int main() {
  const std::vector<Test> tests = {
      {"health_confirmed_hit", test_health_confirmed_hit},
      {"velocity_confirmed_hit", test_velocity_confirmed_hit},
      {"confirmation_before_hurt", test_confirmation_before_hurt},
      {"health_before_hurt", test_health_before_hurt},
      {"confirmation_after_hurt", test_confirmation_after_hurt},
      {"swing_after_hurt", test_swing_after_hurt},
      {"absorption_fallback", test_absorption_fallback},
      {"two_legitimate_hits", test_two_legitimate_hits},
      {"attacker_movement_prefers_closest_swing", test_attacker_movement_prefers_closest_swing},
      {"unknown_team_with_strong_evidence", test_unknown_team_with_strong_evidence},
      {"blocking_without_hurt", test_blocking_without_hurt},
      {"hurt_without_blocking", test_hurt_without_blocking},
      {"blocking_with_non_sword", test_blocking_with_non_sword},
      {"swing_without_hurt", test_swing_without_hurt},
      {"health_without_swing", test_health_without_swing},
      {"velocity_without_swing", test_velocity_without_swing},
      {"swing_outside_time_window", test_swing_outside_time_window},
      {"swing_outside_range", test_swing_outside_range},
      {"same_team_swing", test_same_team_swing},
      {"local_player_swing", test_local_player_swing},
      {"non_player_swing", test_non_player_swing},
      {"hurt_other_entity", test_hurt_other_entity},
      {"arrow_like_damage_without_swing", test_arrow_like_damage_without_swing},
      {"fall_damage", test_fall_damage},
      {"fire_damage", test_fire_damage},
      {"lava_damage", test_lava_damage},
      {"drowning_damage", test_drowning_damage},
      {"suffocation_damage", test_suffocation_damage},
      {"void_damage", test_void_damage},
      {"melee_while_burning_requires_velocity", test_melee_while_burning_requires_velocity},
      {"explosion_before_hurt", test_explosion_before_hurt},
      {"explosion_after_hurt", test_explosion_after_hurt},
      {"explosion_veto_boundaries", test_explosion_veto_boundaries},
      {"world_reset", test_world_reset},
      {"respawn_reset", test_respawn_reset},
      {"disconnect_reset", test_disconnect_reset},
      {"entity_replacement_reset", test_entity_replacement_reset},
      {"local_state_unavailable_reset", test_local_state_unavailable_reset},
      {"disable_reenable_clears", test_disable_reenable_clears},
      {"duplicate_hurt_one_sound", test_duplicate_hurt_one_sound},
      {"one_swing_not_reused", test_one_swing_not_reused},
      {"multiple_swings_without_hurt", test_multiple_swings_without_hurt},
      {"nearby_swing_with_environmental_damage", test_nearby_swing_with_environmental_damage},
      {"debug_independence", test_debug_independence},
      {"swing_before_boundaries", test_swing_before_boundaries},
      {"swing_after_boundaries", test_swing_after_boundaries},
      {"range_boundaries", test_range_boundaries},
      {"confirmation_before_boundaries", test_confirmation_before_boundaries},
      {"confirmation_after_boundaries", test_confirmation_after_boundaries},
      {"fallback_wait_boundary", test_fallback_wait_boundary},
      {"duplicate_boundary", test_duplicate_boundary},
      {"debounce_boundary", test_debounce_boundary},
      {"invalid_and_regressing_timestamps", test_invalid_and_regressing_timestamps},
      {"extremely_large_timestamps", test_extremely_large_timestamps},
      {"invalid_health", test_invalid_health},
      {"rapid_bursts_are_bounded", test_rapid_bursts_are_bounded},
  };

  int failed = 0;
  for (const auto &[name, test] : tests) {
    try {
      test();
      std::cout << "[PASS] " << name << '\n';
    } catch (const std::exception &error) {
      ++failed;
      std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() - static_cast<std::size_t>(failed) << "/"
            << tests.size() << " tests passed\n";
  return failed == 0 ? 0 : 1;
}
