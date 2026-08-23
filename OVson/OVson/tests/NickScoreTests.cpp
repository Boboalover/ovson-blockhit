// Off-game tests for the nickname scorer.
//   ctest -R OVson.NickScore
//
// The weights are not re-derived here; they were fitted against 2075 captured
// names in nickname-scoring-lab and are pinned by NickScoreParity.cpp, which
// replays every one of those names through this engine and the JavaScript one
// and fails on any disagreement. What this file pins is behaviour a person
// would notice: which names pass, which patterns are caught, and the rules
// that were wrong once and must not come back.
#include "../Logic/NickRoll/NickScore.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what) {
  if (!condition) {
    std::printf("FAIL %s\n", what.c_str());
    ++g_failures;
  }
}

int score(const std::string &name) { return OVson::NickRoll::scoreNickname(name, 70).score; }

bool hasTell(const std::string &name, const std::string &id) {
  for (const auto &tell : OVson::NickRoll::scoreNickname(name, 70).tells) {
    if (tell.id == id) return true;
  }
  return false;
}

void expectPass(const std::string &name) {
  check(score(name) >= 70, name + " should pass, scored " + std::to_string(score(name)));
}
void expectFail(const std::string &name) {
  check(score(name) < 70, name + " should fail, scored " + std::to_string(score(name)));
}

} // namespace

int main() {
  // ---- validation ------------------------------------------------------
  check(!OVson::NickRoll::scoreNickname("ab", 70).valid, "two characters is not a name");
  check(!OVson::NickRoll::scoreNickname("abcdefghijklmnopq", 70).valid, "seventeen characters is not a name");
  check(!OVson::NickRoll::scoreNickname("bad name", 70).valid, "a space is not legal");
  check(OVson::NickRoll::scoreNickname("Alpha_42", 70).valid, "underscores and digits are legal");
  check(OVson::NickRoll::scoreNickname("ab", 70).score == 0, "an invalid name scores zero");

  // ---- the two axes are independent ------------------------------------
  {
    const auto result = OVson::NickRoll::scoreNickname("AmyDunn2015", 70);
    check(result.tell >= 90, "person name plus year is a near-certain tell");
    check(result.score <= 100 - result.tell, "the score never exceeds the cap the tell sets");
    check(result.cappedByTell, "and it reports that it was capped");
  }
  for (const char *name : {"Evie77", "DeadlyAir", "freddieburns1997", "XxWolfyMattxX", "Zerqz"}) {
    const auto result = OVson::NickRoll::scoreNickname(name, 70);
    const int expected = std::max(0, std::min(result.appeal, 100 - result.tell));
    check(result.score == expected, std::string(name) + ": score must be min(appeal, 100 - tell)");
  }

  // ---- the names boboa flagged ------------------------------------------
  // Real-looking handles the previous engine rejected for containing ordinary
  // words. Every one of these is a regression if it stops passing.
  for (const char *name : {"maxfly", "Evie77", "Evie7", "DeadlyAir", "True", "max_",
                           "Kai", "Zerqz", "Pyroh", "Vexilo", "TommyInnit", "IcyWater"}) {
    expectPass(name);
  }
  // Long handles. The length curve used to drop these into the 40s for no
  // reason other than being eleven characters.
  for (const char *name : {"officialgod", "Technoblade", "Fruitberries", "atomicunit", "toxicbuilder"}) {
    expectPass(name);
  }
  // Short names carrying a digit, including a leading one. Nothing in either
  // corpus leets a name of six characters or fewer.
  for (const char *name : {"0nyx", "4rr0w", "bl4ze", "Kai86"}) {
    check(!hasTell(name, "leet"), std::string(name) + " must not read as leetspeak");
    expectPass(name);
  }

  // ---- textbook generator output ----------------------------------------
  for (const char *name : {"AmyDunn2015", "freddieburns1997", "zacharysanchez13", "XxWolfyMattxX",
                           "LastAndMax", "The_Golden_Joe", "Th3CoolT3am", "ImLiz49", "TheAlex333",
                           "ItzArchie725", "Crafty_Aidan_90", "amydunn2015", "itztimmy",
                           "lastandmax_9", "thetoxicmeow", "savagenewsub", "icygirlyaxel",
                           "NoraFenwick2011", "SelmaHagberg1997"}) {
    expectFail(name);
  }

  // ---- individual tells --------------------------------------------------
  check(hasTell("ZacharySanchez", "person_name"), "first name plus surname");
  check(hasTell("zacharysanchez", "person_name"), "casing must not matter");
  check(hasTell("Falcon2007", "year_suffix"), "a year suffix");
  check(!hasTell("Falcon9999", "year_suffix"), "9999 is not a year");
  check(hasTell("carter144", "three_digits"), "three trailing digits");
  check(!hasTell("Evie77", "three_digits"), "two trailing digits are not");
  check(hasTell("XxWolfyMattxX", "xx_wrapper"), "the Xx wrapper");
  check(hasTell("LastAndMax", "and_infix"), "an and-compound");
  check(hasTell("ItzTimmy", "stock_prefix"), "a stock prefix with a stock word after it");
  check(!hasTell("Theodore", "stock_prefix"), "Theodore starts with The but is one word");
  check(hasTell("ColdMarc", "adjective_name"), "adjective plus a given name");
  check(!hasTell("DeadlyAir", "adjective_name"), "adjective plus a noun is not");
  check(hasTell("DerpyIron7", "compound_digit"), "a compound with a filler digit");
  check(!hasTell("Evie77", "compound_digit"), "one word with a digit is not");
  check(hasTell("Cute_Herr", "pool_underscore"), "two stock words split by an underscore");
  check(!hasTell("IcyWater", "pool_underscore"), "the same words without the underscore are not");
  check(hasTell("Th3CoolT3am", "leet"), "leetspeak in a long name");

  // "and" inside a word is not a connector. Testing the lowercased string
  // made "ToxicHandMiss" and "The_Sans_Hand" read as X-and-Y compounds.
  check(!hasTell("ToxicHandMiss", "and_infix"), "Hand is not an and-compound");
  check(!hasTell("The_Sans_Hand", "and_infix"), "neither is _Hand");

  // ---- the guards that were wrong once -----------------------------------
  // Many English surnames are ordinary words. Splitting on those turned three
  // perfectly good handles into personas.
  for (const char *name : {"Moonstone", "Evergreen", "Silverstone", "Brightwood"}) {
    check(!hasTell(name, "person_name"), std::string(name) + " is not a person");
  }
  check(!hasTell("maxfly", "person_name"), "a given name glued to a noun is not a person");
  check(!hasTell("TommyInnit", "person_name"), "nor is one of the best-known real accounts");

  // A partial vocabulary match inside a name is not a split.
  for (const char *name : {"officialgod", "technoblade", "atomicunit", "toxicbuilder",
                           "kaanekers", "georgenotfound", "captainsparklez"}) {
    check(OVson::NickRoll::scoreNickname(name, 70).parts.size() < 3,
          std::string(name) + " must not be chopped into a word pile");
  }

  // ---- threshold ---------------------------------------------------------
  check(OVson::NickRoll::scoreNickname("DeadlyAir", 56).passes, "DeadlyAir passes at 56");
  check(!OVson::NickRoll::scoreNickname("Th3CoolT3am", 56).passes, "Th3CoolT3am does not");
  check(OVson::NickRoll::scoreNickname("officialgod", 100).passes == false, "nothing passes at 100 unless perfect");
  check(OVson::NickRoll::scoreNickname("Kai", 0).passes, "everything valid passes at 0");
  // Out-of-range thresholds are clamped rather than rejected.
  check(OVson::NickRoll::scoreNickname("Kai", 500).threshold == 100, "threshold clamps high");
  check(OVson::NickRoll::scoreNickname("Kai", -5).threshold == 0, "threshold clamps low");

  // ---- determinism -------------------------------------------------------
  check(score("The_Cute_Zero_17") == score("The_Cute_Zero_17"), "scoring is deterministic");

  // ---- vocabulary --------------------------------------------------------
  // A build without the private header still runs; it is just blunter. Say so
  // loudly rather than letting it look like a working build.
  if (!OVson::NickRoll::vocabularyAvailable()) {
    std::printf("WARNING: built without NickScoreVocabulary.h; word-aware rules are inert.\n");
  }

  std::printf(g_failures ? "%d check(s) failed.\n" : "All checks passed.\n", g_failures);
  return g_failures ? 1 : 0;
}
