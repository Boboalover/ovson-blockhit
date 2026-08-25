// Emits the private vocabulary header for the C++ port straight out of
// js/scorer.js, so the two engines can never drift by transcription.
"use strict";
const fs = require("fs");
const src = fs.readFileSync(process.argv[2] || "js/scorer.js", "utf8");
const seed = require(require("path").resolve(process.argv[3]));

function grabSet(name) {
  const m = new RegExp("const " + name + " = new Set\\(\\[([\\s\\S]*?)\\]\\);").exec(src);
  if (!m) throw new Error("missing set " + name);
  return m[1].match(/"[^"]*"/g).map(s => JSON.parse(s));
}
function grabArray(name) {
  const m = new RegExp("const " + name + " = \\[([\\s\\S]*?)\\n  \\];").exec(src);
  if (!m) throw new Error("missing array " + name);
  return m[1].match(/"[^"]*"/g).map(s => JSON.parse(s));
}
// Scan BACKWARDS from the .forEach to its own opening bracket. A non-greedy
// regex here starts at the earliest "[" in the file that eventually reaches
// this text, which swallowed the whole FIRST_NAMES set plus every quoted
// string in the comments between -- "None", "Technoblade", "a", "o", "t" all
// ended up in the header and quietly changed four scores.
function grabPush(setName) {
  const marker = "].forEach(function (name) { " + setName + ".add";
  const end = src.indexOf(marker);
  if (end < 0) return [];
  let depth = 0;
  let start = -1;
  for (let index = end; index >= 0; index -= 1) {
    if (src[index] === "]") depth += 1;
    else if (src[index] === "[") {
      depth -= 1;
      if (depth === 0) { start = index; break; }
    }
  }
  if (start < 0) throw new Error("unbalanced list for " + setName);
  const body = src.slice(start + 1, end);
  return (body.match(/"[^"]*"/g) || []).map(s => JSON.parse(s));
}

const sets = {
  FIRST_NAMES: [...new Set(grabSet("FIRST_NAMES").concat(grabPush("FIRST_NAMES")))].sort(),
  SURNAMES: grabSet("SURNAMES").sort(),
  POOL_WORDS: grabSet("POOL_WORDS").sort(),
  ADJECTIVES: grabSet("ADJECTIVES").sort(),
  AMBIGUOUS_SURNAMES: grabSet("AMBIGUOUS_SURNAMES").sort(),
  SEGMENT_WORDS: [...new Set(grabArray("SEGMENT_WORDS"))].sort(),
  SEED_WORDS: seed.words.slice().sort(),
  SEED_NAMES: seed.names.slice().sort()
};

let out = `#pragma once

// ---------------------------------------------------------------------------
// PRIVATE DATA -- NOT COMMITTED.
//
// Generated from nickname-scoring-lab/js/scorer.js. Do not hand-edit: rerun
// tools/GenerateNickScoreVocabulary.ps1 instead, or the C++ engine and the
// lab will drift apart silently.
//
// This file is listed in .gitignore. A public checkout builds without it and
// falls back to shape-only scoring (see NickScore.cpp), which still catches
// years, digit tails, Xx wrappers, word piles and leetspeak -- everything that
// does not need to know a word.
//
// Honest note on secrecy: keeping this out of the repository keeps it out of
// the public source. It does NOT keep it out of the shipped DLL; anyone with
// the binary and a copy of \`strings\` can recover the lists. Encrypting them at
// rest would only raise that bar slightly while making the binary look more
// like something worth quarantining to a heuristic scanner, which this project
// has already been bitten by once.
// ---------------------------------------------------------------------------

#define OVSON_NICKSCORE_HAS_VOCABULARY 1

namespace OVson::NickRoll::Vocabulary {

`;
for (const [name, words] of Object.entries(sets)) {
  out += `inline constexpr const char *k${name.split("_").map(p => p[0] + p.slice(1).toLowerCase()).join("")}[] = {\n`;
  for (let i = 0; i < words.length; i += 8) {
    out += "    " + words.slice(i, i + 8).map(w => JSON.stringify(w)).join(", ") + ",\n";
  }
  out += "};\n\n";
}
out += "} // namespace OVson::NickRoll::Vocabulary\n";
fs.writeFileSync(process.argv[4] || "/home/claude/work/port/NickScoreVocabulary.h", out);
console.log(Object.entries(sets).map(([k, v]) => k + "=" + v.length).join("  "));
