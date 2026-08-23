#include "../Logic/NickRoll/NickRollCore.h"

#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace OVson::NickRoll;

namespace {

int g_failures = 0;

void require(bool condition, const char *what) {
  if (!condition)
    throw std::runtime_error(what);
}

// Shaped like a real Hypixel book page: a sentence, the name on its own line,
// then two clickable buttons.
const char *kRealisticPage = R"JSON(
{"text":"","extra":[
  {"text":"We've generated a random username for you:\n","color":"black"},
  {"text":"FelicityM1998\n\n","color":"black","bold":true},
  {"text":"USE NAME","color":"green","bold":true,
   "clickEvent":{"action":"run_command","value":"/nickcallback confirm"}},
  {"text":"\n"},
  {"text":"TRY AGAIN","color":"red","strikethrough":true,
   "clickEvent":{"action":"run_command","value":"/nickcallback reroll"}}
]}
)JSON";

void realisticPageIsReadEndToEnd() {
  const BookPage page = parsePageJson(kRealisticPage);
  require(page.parsed, "the realistic page did not parse");
  require(isGeneratedNamePage(page), "the generated-name page was not recognised");
  require(findGeneratedName(page) == "FelicityM1998", "the name was not extracted");
}

// "USE" is a legal Minecraft username, so a naive scan of the page text picks
// it up before ever reaching the real name. The buttons are clickable and the
// name is not, which is what keeps them apart.
void buttonLabelsAreNeverMistakenForTheName() {
  const BookPage page = parsePageJson(kRealisticPage);
  require(findGeneratedName(page) != "USE", "USE NAME was read as the username");
  require(findGeneratedName(page) != "NAME", "USE NAME was read as the username");
  require(findGeneratedName(page) != "TRY", "TRY AGAIN was read as the username");
}

void nameSharingASegmentWithTheSentenceIsStillFound() {
  const BookPage page = parsePageJson(
      R"JSON({"text":"We've generated a random username for you:\nQuietRiver77\n"})JSON");
  require(page.parsed, "single-segment page did not parse");
  require(findGeneratedName(page) == "QuietRiver77",
          "a name sharing the sentence's segment was missed");
}

void otherBookPagesProduceNothing() {
  const char *rankPage =
      R"JSON({"text":"Let's get you set up with your nickname! First, you'll need to choose which RANK you would like to be shown as when nicked."})JSON";
  const BookPage page = parsePageJson(rankPage);
  require(page.parsed, "the rank page did not parse");
  require(!isGeneratedNamePage(page), "the rank page was treated as the name page");
  require(findGeneratedName(page).empty(), "the rank page produced a name");
}

// A page that cannot be read must leave the caller doing nothing at all. A
// wrong action here costs one of six daily /nick uses; doing nothing costs a
// manual reroll.
void malformedJsonIsRefusedRatherThanSalvaged() {
  const std::vector<std::string> broken = {
      "", "   ", "{", "{\"text\":}", "{\"text\":\"a\"", "not json at all",
      R"({"text":"a","extra":[{"text":"b"})",
      R"({"text":"We've generated a random username for you:\nBob99")"};
  for (const auto &json : broken) {
    const BookPage page = parsePageJson(json);
    require(!page.parsed, "malformed JSON reported itself as parsed");
    require(findGeneratedName(page).empty(), "malformed JSON produced a name");
  }
}

void usernameRulesMatchMinecraft() {
  require(isValidUsername("Bob"), "3 characters is legal");
  require(isValidUsername("A234567890123456"), "16 characters is legal");
  require(!isValidUsername("ab"), "2 characters is not legal");
  require(!isValidUsername("A2345678901234567"), "17 characters is not legal");
  require(isValidUsername("_x_"), "underscores are legal");
  require(!isValidUsername("has space"), "spaces are not legal");
  require(!isValidUsername("dash-name"), "dashes are not legal");
  require(!isValidUsername(""), "the empty string is not a username");
}

void formattingCodesAreStrippedFromPageText() {
  const BookPage page = parsePageJson(
      "{\"text\":\"We've generated a random username for you:\\n\\u00a7lBoldName99\\n\"}");
  require(page.parsed, "page with formatting codes did not parse");
  require(findGeneratedName(page) == "BoldName99",
          "a formatting code leaked into the extracted name");
}

struct Case {
  const char *name;
  std::function<void()> run;
};


// The reroll command is READ off the page, never assembled. Hypixel is free to
// change what TRY AGAIN runs; a hard-coded "/nick help setrandom" would then
// spend one of the six daily uses on a command that no longer means reroll.
void theRerollCommandIsReadFromThePage() {
  const BookPage page = parsePageJson(kRealisticPage);
  require(findButtonCommand(page, "try again") == "/nickcallback reroll",
          "the TRY AGAIN command was not read");
  require(findButtonCommand(page, "TRY AGAIN") == "/nickcallback reroll",
          "the label match must ignore case");
  require(findButtonCommand(page, "use name") == "/nickcallback confirm",
          "the USE NAME command was not read");
  require(findButtonCommand(page, "cancel").empty(),
          "a label that is not on the page must yield nothing");
}

// A page offering the button under some other click action is a page we do not
// understand, and the right response to not understanding is to do nothing.
void aButtonThatIsNotRunCommandIsRefused() {
  const BookPage page = parsePageJson(
      R"JSON({"text":"","extra":[
        {"text":"We've generated a random username for you:\nQuietRiver77\n"},
        {"text":"TRY AGAIN","clickEvent":{"action":"change_page","value":"2"}}
      ]})JSON");
  require(page.parsed, "the page did not parse");
  require(findButtonCommand(page, "try again").empty(),
          "change_page must not be actioned");
}

} // namespace

int main() {
  const std::vector<Case> cases = {
      {"realistic page end to end", realisticPageIsReadEndToEnd},
      {"button labels are not the name", buttonLabelsAreNeverMistakenForTheName},
      {"name shares the sentence segment", nameSharingASegmentWithTheSentenceIsStillFound},
      {"other book pages do nothing", otherBookPagesProduceNothing},
      {"malformed json is refused", malformedJsonIsRefusedRatherThanSalvaged},
      {"username rules", usernameRulesMatchMinecraft},
      {"formatting codes stripped", formattingCodesAreStrippedFromPageText},
      {"reroll command read from page", theRerollCommandIsReadFromThePage},
      {"non run_command button refused", aButtonThatIsNotRunCommandIsRefused},
  };
  for (const auto &item : cases) {
    try {
      item.run();
      std::printf("PASS %s\n", item.name);
    } catch (const std::exception &error) {
      ++g_failures;
      std::printf("FAIL %s: %s\n", item.name, error.what());
    }
  }
  std::printf("\n%d/%d passed\n", static_cast<int>(cases.size()) - g_failures,
              static_cast<int>(cases.size()));
  return g_failures ? 1 : 0;
}
