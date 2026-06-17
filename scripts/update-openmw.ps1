param(
    [string]$OpenMWRemoteName = "openmw-upstream",
    [string]$OpenMWRemoteUrl = "https://github.com/OpenMW/openmw.git",
    [string]$OpenMWBranch = "master",
    [string]$TargetBranch = "openmw-master",
    [string]$SyncBranch = "",
    [string]$PushRemote = "",
    [string]$BuildDir = "build\tes3mp-openmw",
    [string]$Configuration = "RelWithDebInfo",
    [string]$Generator = "Ninja",
    [string]$GnsVcpkgRepository = "https://github.com/microsoft/vcpkg.git",
    [string]$GnsVcpkgDir = "",
    [string]$GnsTriplet = "x64-windows",
    [string]$CMakeToolchainFile = $env:CMAKE_TOOLCHAIN_FILE,
    [string]$CMakePrefixPath = $env:CMAKE_PREFIX_PATH,
    [string]$PacketTestFilter = "GnsTransportTest.*:MpBasePacketTest.*",
    [switch]$InstallGnsDependencies,
    [switch]$RunPacketTests,
    [switch]$RunInstallSmoke,
    [switch]$NoBuild,
    [switch]$Push,
    [switch]$ForcePush,
    [switch]$AllowDirty
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

function Get-GitOutput {
    param([string[]]$Arguments)

    $output = & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git exited with code $LASTEXITCODE"
    }
    return $output
}

function Convert-ToAbsolutePath {
    param(
        [string]$Path,
        [string]$BasePath
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $Path
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Add-CMakePrefixPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    $separator = [System.IO.Path]::PathSeparator
    $entries = @()
    if (-not [string]::IsNullOrWhiteSpace($script:CMakePrefixPath)) {
        $entries = $script:CMakePrefixPath -split [Regex]::Escape($separator)
    }

    if ($entries -notcontains $Path) {
        if ([string]::IsNullOrWhiteSpace($script:CMakePrefixPath)) {
            $script:CMakePrefixPath = $Path
        } else {
            $script:CMakePrefixPath = "$script:CMakePrefixPath$separator$Path"
        }
    }
}

function Initialize-GnsVcpkg {
    param([string]$RepoRoot)

    if ([string]::IsNullOrWhiteSpace($script:GnsVcpkgDir)) {
        $script:GnsVcpkgDir = Join-Path (Split-Path -Parent $RepoRoot) "vcpkg-gns"
    }

    $script:GnsVcpkgDir = Convert-ToAbsolutePath $script:GnsVcpkgDir $RepoRoot

    if (-not (Test-Path (Join-Path $script:GnsVcpkgDir ".git"))) {
        if (Test-Path $script:GnsVcpkgDir) {
            throw "GNS vcpkg directory exists but is not a git checkout: $script:GnsVcpkgDir"
        }
        Invoke-Checked git @("clone", "--depth", "1", $GnsVcpkgRepository, $script:GnsVcpkgDir)
    } else {
        Invoke-Checked git @("-C", $script:GnsVcpkgDir, "fetch", "--depth", "1", "origin", "master")
        Invoke-Checked git @("-C", $script:GnsVcpkgDir, "checkout", "-q", "master")
        Invoke-Checked git @("-C", $script:GnsVcpkgDir, "reset", "--hard", "-q", "origin/master")
    }

    $vcpkgExe = Join-Path $script:GnsVcpkgDir "vcpkg.exe"
    if (-not (Test-Path $vcpkgExe)) {
        Invoke-Checked (Join-Path $script:GnsVcpkgDir "bootstrap-vcpkg.bat") @("-disableMetrics")
    }

    Invoke-Checked $vcpkgExe @("install", "gamenetworkingsockets", "boost-asio", "boost-iostreams", "--triplet", $GnsTriplet)
    Add-CMakePrefixPath (Join-Path $script:GnsVcpkgDir "installed\$GnsTriplet")
}

function Invoke-PacketTests {
    param(
        [string]$BuildDir,
        [string]$Configuration,
        [string]$Filter
    )

    $candidates = @(
        (Join-Path $BuildDir "$Configuration\components-tests.exe"),
        (Join-Path $BuildDir "components-tests.exe"),
        (Join-Path $BuildDir "$Configuration\components-tests"),
        (Join-Path $BuildDir "components-tests")
    )

    $testExecutable = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($testExecutable)) {
        throw "components-tests executable was not found under $BuildDir"
    }

    Invoke-Checked $testExecutable @("--gtest_filter=$Filter")
}

function Assert-BuiltExecutable {
    param(
        [string]$BuildDir,
        [string]$Configuration,
        [string]$Name
    )

    $candidates = @(
        (Join-Path $BuildDir "$Configuration\$Name"),
        (Join-Path $BuildDir $Name)
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return
        }
    }

    throw "Expected build output was not found: $Name under $BuildDir"
}

function Assert-NotBuiltExecutable {
    param(
        [string]$BuildDir,
        [string]$Configuration,
        [string]$Name
    )

    $candidates = @(
        (Join-Path $BuildDir "$Configuration\$Name"),
        (Join-Path $BuildDir $Name)
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            throw "Obsolete build output is still present: $candidate"
        }
    }
}

function Invoke-InstallSmoke {
    param(
        [string]$RepoRoot,
        [string]$BuildDir,
        [string]$Configuration
    )

    Invoke-Checked powershell @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $RepoRoot "scripts\verify-tes3mp-install.ps1"),
        "-BuildDir", $BuildDir,
        "-Configuration", $Configuration,
        "-WithLocalMaster"
    )
}

$repoRoot = (Get-GitOutput @("rev-parse", "--show-toplevel")).Trim()
Set-Location $repoRoot

if (-not $AllowDirty) {
    $dirty = Get-GitOutput @("status", "--porcelain")
    if (-not [string]::IsNullOrWhiteSpace($dirty)) {
        throw "Working tree is not clean. Commit, stash, or pass -AllowDirty."
    }
}

if ([string]::IsNullOrWhiteSpace($TargetBranch)) {
    $TargetBranch = (Get-GitOutput @("rev-parse", "--abbrev-ref", "HEAD")).Trim()
}

$remoteNames = @(Get-GitOutput @("remote"))
if ($remoteNames -contains $OpenMWRemoteName) {
    Invoke-Checked git @("remote", "set-url", $OpenMWRemoteName, $OpenMWRemoteUrl)
} else {
    Invoke-Checked git @("remote", "add", $OpenMWRemoteName, $OpenMWRemoteUrl)
}

Invoke-Checked git @("fetch", $OpenMWRemoteName, $OpenMWBranch)

$localBranches = @(Get-GitOutput @("branch", "--format=%(refname:short)"))
if ($localBranches -contains $TargetBranch) {
    Invoke-Checked git @("checkout", $TargetBranch)
} else {
    Invoke-Checked git @("checkout", "-B", $TargetBranch)
}

if (-not [string]::IsNullOrWhiteSpace($SyncBranch)) {
    Invoke-Checked git @("checkout", "-B", $SyncBranch, $TargetBranch)
}

$upstreamRef = "$OpenMWRemoteName/$OpenMWBranch"
$beforeMerge = (Get-GitOutput @("rev-parse", "HEAD")).Trim()
$upstreamCommit = (Get-GitOutput @("rev-parse", $upstreamRef)).Trim()

Write-Host "Merging $upstreamRef ($upstreamCommit) into $(Get-GitOutput @("rev-parse", "--abbrev-ref", "HEAD"))"
Invoke-Checked git @("merge", "--no-edit", $upstreamRef)

$afterMerge = (Get-GitOutput @("rev-parse", "HEAD")).Trim()
if ($beforeMerge -eq $afterMerge) {
    Write-Host "No OpenMW changes were available to merge."
} else {
    Write-Host "Merged OpenMW into $afterMerge."
}

if (-not $NoBuild) {
    $BuildDir = Convert-ToAbsolutePath $BuildDir $repoRoot

    if ($InstallGnsDependencies) {
        Initialize-GnsVcpkg $repoRoot
    }

    $cmakeArgs = @(
        "-S", $repoRoot,
        "-B", $BuildDir,
        "-G", $Generator,
        "-D", "CMAKE_BUILD_TYPE=$Configuration",
        "-D", "BUILD_TES3MP_CLIENT=ON",
        "-D", "BUILD_TES3MP_SERVER=ON",
        "-D", "BUILD_COMMUNITYMP_HUB=ON",
        "-D", "BUILD_TES3MP_MASTER=ON",
        "-D", "BUILD_COMPONENTS_TESTS=ON"
    )

    if (-not [string]::IsNullOrWhiteSpace($CMakeToolchainFile)) {
        $cmakeArgs += @("-D", "CMAKE_TOOLCHAIN_FILE=$CMakeToolchainFile")
    }

    if (-not [string]::IsNullOrWhiteSpace($CMakePrefixPath)) {
        $cmakeArgs += @("-D", "CMAKE_PREFIX_PATH=$CMakePrefixPath")
    }

    Invoke-Checked cmake $cmakeArgs
    Invoke-Checked cmake @(
        "--build", $BuildDir,
        "--config", $Configuration,
        "--target", "communitymp", "openmw", "tes3mp-server", "communitymp-hub", "masterserver", "components-tests"
    )

    foreach ($expectedOutput in @("communitymp.exe", "communitymp-client.exe", "communitymp-server.exe", "communitymp-hub.exe", "masterserver.exe", "components-tests.exe")) {
        Assert-BuiltExecutable $BuildDir $Configuration $expectedOutput
    }
    foreach ($obsoleteOutput in @("tes3mp.exe", "tes3mp-server.exe")) {
        Assert-NotBuiltExecutable $BuildDir $Configuration $obsoleteOutput
    }

    if ($RunPacketTests) {
        Invoke-PacketTests $BuildDir $Configuration $PacketTestFilter
    }

    if ($RunInstallSmoke) {
        Invoke-InstallSmoke $repoRoot $BuildDir $Configuration
    }
}

if ($Push) {
    $currentBranch = (Get-GitOutput @("rev-parse", "--abbrev-ref", "HEAD")).Trim()
    if ([string]::IsNullOrWhiteSpace($PushRemote)) {
        $PushRemote = (git config "branch.$TargetBranch.remote")
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($PushRemote)) {
            $PushRemote = "origin"
        }
    }

    $pushArgs = @("push")
    if ($ForcePush) {
        $pushArgs += "--force-with-lease"
    }
    $pushArgs += @($PushRemote, "HEAD:$currentBranch")
    Invoke-Checked git $pushArgs
}
