# build_msix.ps1 -- package minSolo.exe into an MSIX (QCView's flow without the Qt deployment).
#
#   cmake --build build --target minsolo_msix
# or
#   powershell -File msix\build_msix.ps1 -BuildDir build -Version 0.1.0 -ManifestPath build\msix\AppxManifest.xml
#
# Signing: set MINSOLO_SIGNING_PFX (absolute path), optionally MINSOLO_SIGNING_PASSWORD and
# MINSOLO_TIMESTAMP_URL, to produce a signed sideload MSIX. Unset = unsigned, the Store artifact
# (Microsoft signs centrally). The PFX subject must equal the manifest Publisher.
# ASCII-only on purpose for Windows PowerShell 5.1.

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter()]          [string] $OutputDir = (Join-Path $PSScriptRoot "dist"),
    [Parameter()]          [string] $ManifestPath = (Join-Path $PSScriptRoot "AppxManifest.xml"),
    [Parameter()]          [string] $ImagesDir   = (Join-Path $PSScriptRoot "images")
)
$ErrorActionPreference = "Stop"

function Find-SdkTool {
    param([string] $name)
    foreach ($root in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")) {
        if (-not (Test-Path $root)) { continue }
        $hits = Get-ChildItem -Path $root -Recurse -Filter $name -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "\\x64\\" }
        if ($hits) { return ($hits | Sort-Object FullName -Descending | Select-Object -First 1).FullName }
    }
    return $null
}
$MakeAppx = Find-SdkTool "MakeAppx.exe"
$MakePri  = Find-SdkTool "MakePri.exe"
$SignTool = Find-SdkTool "signtool.exe"
if (-not $MakeAppx) { throw "MakeAppx.exe not found. Install the Windows 10/11 SDK." }
if (-not $MakePri)  { throw "MakePri.exe not found. Install the Windows 10/11 SDK." }
Write-Host "MakeAppx : $MakeAppx"

$exe = Join-Path $BuildDir "minSolo.exe"
if (-not (Test-Path $exe)) { throw "minSolo.exe not found at $exe. Build first." }
if (-not (Test-Path $ManifestPath)) { throw "AppxManifest.xml not found at $ManifestPath (re-run CMake configure)." }
if (-not (Test-Path $ImagesDir)) { throw "Tile images dir not found at $ImagesDir." }

$staging = Join-Path $PSScriptRoot "staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Path $staging | Out-Null
Copy-Item -Path $exe -Destination $staging
Copy-Item -Path $ManifestPath -Destination (Join-Path $staging "AppxManifest.xml")
Copy-Item -Recurse -Path $ImagesDir -Destination (Join-Path $staging "images")
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
foreach ($leaf in @("LICENSE", "PRIVACY_POLICY.md")) {
    $p = Join-Path $repoRoot $leaf
    if (Test-Path $p) { Copy-Item -Path $p -Destination $staging }
}

Write-Host "Generating resources.pri ..."
$priConfig = Join-Path $staging "priconfig.xml"
& $MakePri createconfig /cf $priConfig /dq "en-us" /o | Out-Null
& $MakePri new /pr $staging /cf $priConfig /mn (Join-Path $staging "AppxManifest.xml") /of (Join-Path $staging "resources.pri") /o
if ($LASTEXITCODE -ne 0) { throw "MakePri new failed (exit $LASTEXITCODE)" }
Remove-Item -Path $priConfig

if (-not (Test-Path $OutputDir)) { New-Item -ItemType Directory -Path $OutputDir | Out-Null }
$pfx = $env:MINSOLO_SIGNING_PFX
$suffix = if ($pfx) { "-sideload" } else { "" }
$output = Join-Path $OutputDir "minSolo-${Version}-win64${suffix}.msix"
if (Test-Path $output) { Remove-Item -Path $output }
Write-Host "Packing $output ..."
& $MakeAppx pack /d $staging /p $output /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx pack failed (exit $LASTEXITCODE)" }
Remove-Item -Recurse -Force $staging

if ($pfx -and (Test-Path $pfx)) {
    if (-not $SignTool) { Write-Warning "MINSOLO_SIGNING_PFX set but signtool.exe not found -- left unsigned." }
    else {
        Write-Host "Signing $output ..."
        $signArgs = @("sign", "/fd", "SHA256", "/f", $pfx)
        if ($env:MINSOLO_SIGNING_PASSWORD) { $signArgs += @("/p", $env:MINSOLO_SIGNING_PASSWORD) }
        if ($env:MINSOLO_TIMESTAMP_URL) { $signArgs += @("/tr", $env:MINSOLO_TIMESTAMP_URL, "/td", "SHA256") }
        $signArgs += $output
        & $SignTool @signArgs
        if ($LASTEXITCODE -ne 0) { throw "signtool sign failed (exit $LASTEXITCODE)" }
        & $SignTool verify /pa $output | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "signtool verify /pa failed: the package is NOT signed" }
        Write-Host "Signed and verified."
    }
} elseif ($pfx) {
    throw "MINSOLO_SIGNING_PFX is set but does not exist: $pfx (use an absolute path)"
} else {
    Write-Host "MINSOLO_SIGNING_PFX not set -- unsigned MSIX (the Store submission artifact)."
}
Write-Host "SUCCESS: $output"
