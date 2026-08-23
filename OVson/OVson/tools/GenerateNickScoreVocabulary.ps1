# Regenerates the private scoring vocabulary header for the C++ engine.
#
#   powershell -ExecutionPolicy Bypass -File tools/GenerateNickScoreVocabulary.ps1
#
# Reads nickname-scoring-lab/js/scorer.js and writes
# Logic/NickRoll/NickScoreVocabulary.h, which is gitignored. Run it whenever
# the lab's word lists change, or the C++ engine and the lab will drift apart
# silently -- tests/NickScoreParity.cpp exists to catch that, but only if you
# run it.
#
# Node is required. It is already a prerequisite for the lab's own test suite.
[CmdletBinding()]
param(
  [string]$LabPath = "",
  [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (
  Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
)

if ([string]::IsNullOrWhiteSpace($LabPath)) {
  # The lab normally sits beside the fork, not inside it: it is a separate
  # research project and deliberately not part of this repository.
  $LabPath = Join-Path (Split-Path -Parent $repoRoot) "nickname-scoring-lab"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $PSScriptRoot "..\Logic\NickRoll\NickScoreVocabulary.h"
}

$scorer = Join-Path $LabPath "js\scorer.js"
$seed = Join-Path $LabPath "data\seed-lexicon.js"
$generator = Join-Path $PSScriptRoot "GenerateNickScoreVocabulary.js"

foreach ($required in @($scorer, $seed, $generator)) {
  if (-not (Test-Path $required)) {
    throw "Missing $required. Pass -LabPath if nickname-scoring-lab lives somewhere else."
  }
}

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
  throw "node was not found on PATH. It is needed to read the lab's word lists."
}

& node $generator $scorer $seed $OutputPath
if ($LASTEXITCODE -ne 0) { throw "Vocabulary generation failed with exit code $LASTEXITCODE." }

Write-Host "Wrote $((Resolve-Path $OutputPath).Path)"
Write-Host "This file is private and gitignored. Do not commit it."
