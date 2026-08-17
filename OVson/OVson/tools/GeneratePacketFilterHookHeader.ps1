param(
    [string]$NettyJar = "$env:APPDATA\.minecraft\libraries\io\netty\netty-all\4.0.23.Final\netty-all-4.0.23.Final.jar",
    [string]$JavaSource = (Join-Path $PSScriptRoot "..\Logic\PacketFilterHook.java"),
    [string]$OutputHeader = (Join-Path $PSScriptRoot "..\Logic\PacketFilterHook_bytes.h")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $NettyJar)) {
    throw "Netty 4.0.23 jar not found: $NettyJar"
}
if (-not (Test-Path -LiteralPath $JavaSource)) {
    throw "Java hook source not found: $JavaSource"
}
if (-not (Get-Command javac -ErrorAction SilentlyContinue)) {
    throw "javac was not found on PATH"
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("ovson-packet-hook-" + [Guid]::NewGuid().ToString("N"))
$classesDirectory = Join-Path $temporaryRoot "classes"

try {
    New-Item -ItemType Directory -Path $classesDirectory | Out-Null
    & javac --release 8 -cp $NettyJar -d $classesDirectory $JavaSource
    if ($LASTEXITCODE -ne 0) {
        throw "javac failed with exit code $LASTEXITCODE"
    }

    $classFile = Join-Path $classesDirectory `
        "net\ovson\api\hook\PacketFilterHook.class"
    $bytes = [IO.File]::ReadAllBytes($classFile)
    if ($bytes.Length -lt 8 -or $bytes[0] -ne 0xCA -or $bytes[1] -ne 0xFE -or
        $bytes[2] -ne 0xBA -or $bytes[3] -ne 0xBE) {
        throw "javac output is not a valid Java class file"
    }

    $majorVersion = ([int]$bytes[6] -shl 8) -bor [int]$bytes[7]
    if ($majorVersion -ne 52) {
        throw "Expected Java 8 class-file major version 52, got $majorVersion"
    }

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("#pragma once")
    $lines.Add("")
    $lines.Add("// Generated from PacketFilterHook.java by tools/GeneratePacketFilterHookHeader.ps1.")
    $lines.Add("static const unsigned char PacketFilterHook_class[] = {")
    for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
        $last = [Math]::Min($offset + 15, $bytes.Length - 1)
        $hex = for ($index = $offset; $index -le $last; ++$index) {
            "0x{0:x2}" -f $bytes[$index]
        }
        $suffix = if ($last -lt $bytes.Length - 1) { "," } else { "" }
        $lines.Add("  " + ($hex -join ", ") + $suffix)
    }
    $lines.Add("};")
    $lines.Add("static const unsigned int PacketFilterHook_class_len = $($bytes.Length);")

    $resolvedOutput = [IO.Path]::GetFullPath($OutputHeader)
    [IO.File]::WriteAllLines(
        $resolvedOutput, $lines, [Text.UTF8Encoding]::new($false))

    $hash = (Get-FileHash -LiteralPath $classFile -Algorithm SHA256).Hash
    Write-Host "Generated $resolvedOutput"
    Write-Host "Java major version: $majorVersion"
    Write-Host "Embedded bytes: $($bytes.Length)"
    Write-Host "Class SHA-256: $hash"
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
