param(
    [string]$BuildDir = "MSVC2022_64_Ninja",
    [string]$Configuration = "Release",
    [string]$ArtifactRoot = "",
    [string]$PackagePrefix = "CommunityMP-testing",
    [switch]$NoBuild,
    [switch]$Minimal,
    [switch]$StopRunning,
    [switch]$Sign,
    [switch]$SignAllBinaries
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE"
    }
}

function Get-GitLine {
    param([string[]]$Arguments)

    $output = & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git exited with code $LASTEXITCODE"
    }

    return ($output | Select-Object -First 1).Trim()
}

function Assert-PathInsideDirectory {
    param(
        [string]$Path,
        [string]$Parent
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $requiredPrefix = $resolvedParent + [System.IO.Path]::DirectorySeparatorChar

    if (-not $resolvedPath.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside artifact root: $resolvedPath"
    }
}

function Assert-NoDebugArtifacts {
    param([string]$Root)

    $forbiddenPatterns = @("*.pdb", "*.ilk", "*.iobj", "*.ipdb", "*.obj", "*.lib", "*.exp", "*_d.dll")
    $debugFiles = @()
    foreach ($pattern in $forbiddenPatterns) {
        $debugFiles += Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue
    }

    foreach ($unexpectedExe in @("components-tests.exe", "ServerTest.exe")) {
        $path = Join-Path $Root $unexpectedExe
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $debugFiles += Get-Item -LiteralPath $path
        }
    }

    if ($debugFiles.Count -gt 0) {
        $examples = ($debugFiles | Select-Object -First 10 | ForEach-Object { $_.FullName }) -join "`n"
        throw "Package contains debug, test, or build artifacts:`n$examples"
    }
}

$repoRoot = Get-GitLine @("rev-parse", "--show-toplevel")
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($ArtifactRoot)) {
    $ArtifactRoot = Join-Path (Split-Path -Parent $repoRoot) "artifacts"
}

$resolvedArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
New-Item -ItemType Directory -Force -Path $resolvedArtifactRoot | Out-Null

$shortCommit = Get-GitLine @("rev-parse", "--short=10", "HEAD")
$packageName = "$PackagePrefix-$shortCommit-Windows-x64"
$installDir = Join-Path $resolvedArtifactRoot $packageName
$resolvedInstallDir = [System.IO.Path]::GetFullPath($installDir)
$zipPath = "$resolvedInstallDir.zip"
$shaPath = "$zipPath.sha256"

Assert-PathInsideDirectory $resolvedInstallDir $resolvedArtifactRoot
if ((Split-Path -Leaf $resolvedInstallDir) -notlike "$PackagePrefix-*-Windows-x64") {
    throw "Refusing to remove unexpected artifact directory: $resolvedInstallDir"
}

if (Test-Path -LiteralPath $resolvedInstallDir) {
    Remove-Item -LiteralPath $resolvedInstallDir -Recurse -Force
}

foreach ($path in @($zipPath, $shaPath)) {
    Assert-PathInsideDirectory $path $resolvedArtifactRoot
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}

$deployScript = Join-Path $repoRoot "scripts\deploy-tes3mp.ps1"
$deployArgs = @{
    BuildDir = $BuildDir
    Configuration = $Configuration
    InstallDir = $resolvedInstallDir
    NoBuild = $NoBuild.IsPresent
    Minimal = $Minimal.IsPresent
    StopRunning = $StopRunning.IsPresent
}

& $deployScript @deployArgs
if ($LASTEXITCODE -ne 0) {
    throw "$deployScript exited with code $LASTEXITCODE"
}
Assert-NoDebugArtifacts $resolvedInstallDir

if ($Sign) {
    $signScript = Join-Path $repoRoot "scripts\sign-tes3mp-runtime.ps1"
    $signArgs = @{
        InstallDir = $resolvedInstallDir
        ExecutablesOnly = -not $SignAllBinaries.IsPresent
    }

    & $signScript @signArgs
    if ($LASTEXITCODE -ne 0) {
        throw "$signScript exited with code $LASTEXITCODE"
    }
}

Compress-Archive -LiteralPath $resolvedInstallDir -DestinationPath $zipPath -Force
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashLine = "$hash  $(Split-Path -Leaf $zipPath)"
Set-Content -LiteralPath $shaPath -Value $hashLine -Encoding ASCII

Write-Host "CommunityMP package created:"
Write-Host "  Folder: $resolvedInstallDir"
Write-Host "  Zip:    $zipPath"
Write-Host "  SHA256: $shaPath"
