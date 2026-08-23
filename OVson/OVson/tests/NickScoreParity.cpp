// Parity harness: the C++ engine against the JavaScript one.
//
//   OVsonNickScoreParity <fixture.tsv>
//
// The fixture is produced by the lab:
//
//   node tools/emit-parity-fixture.js > nickscore-parity.tsv
//
// and holds one row per name: name, score, tell, appeal, verdict. It is NOT
// committed -- it is a dump of the captured corpus, which is private -- so
// this harness treats a missing fixture as "skipped" rather than as failure.
//
// This is the only thing keeping the two engines honest. The weights live in
// two languages now; without this, a change to one silently diverges from the
// other and the lab stops predicting what the client will do.
#include "../Logic/NickRoll/NickScore.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: OVsonNickScoreParity <fixture.tsv>\n");
    return 0;
  }
  std::ifstream input(argv[1]);
  if (!input) {
    std::printf("SKIP: no parity fixture at %s. Generate it from the lab.\n", argv[1]);
    return 0;
  }

  int rows = 0;
  int mismatches = 0;
  std::string line;
  while (std::getline(input, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty()) continue;

    std::istringstream fields(line);
    std::string name;
    std::string expectedScore;
    std::string expectedTell;
    std::string expectedAppeal;
    std::string expectedVerdict;
    if (!std::getline(fields, name, '\t') || !std::getline(fields, expectedScore, '\t') ||
        !std::getline(fields, expectedTell, '\t') || !std::getline(fields, expectedAppeal, '\t') ||
        !std::getline(fields, expectedVerdict, '\t')) {
      std::printf("MALFORMED ROW: %s\n", line.c_str());
      ++mismatches;
      continue;
    }

    ++rows;
    const auto result = OVson::NickRoll::scoreNickname(name, 70);
    const std::string verdict = result.valid ? result.verdict : "Invalid";
    if (std::to_string(result.score) != expectedScore || std::to_string(result.tell) != expectedTell ||
        std::to_string(result.appeal) != expectedAppeal || verdict != expectedVerdict) {
      if (mismatches < 25) {
        std::printf("MISMATCH %-18s lab: %s/%s/%s %s   cpp: %d/%d/%d %s\n", name.c_str(),
                    expectedScore.c_str(), expectedTell.c_str(), expectedAppeal.c_str(),
                    expectedVerdict.c_str(), result.score, result.tell, result.appeal,
                    verdict.c_str());
      }
      ++mismatches;
    }
  }

  std::printf("%d row(s) compared, %d mismatch(es).\n", rows, mismatches);
  if (rows == 0) {
    std::printf("The fixture was empty. That is a failure, not a pass.\n");
    return 1;
  }
  return mismatches ? 1 : 0;
}
