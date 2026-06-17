param(
    [string] $BuildDir = "C:\tes3mp_refresh\openmw\MSVC2022_64_Ninja",
    [string] $Configuration = "RelWithDebInfo",
    [string] $InstallDir = "",
    [switch] $WithLocalMaster,
    [switch] $Minimal,
    [switch] $KeepInstallDir
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:TEMP ("tes3mp-install-smoke-" + [guid]::NewGuid().ToString("N"))
    $removeInstallDir = -not $KeepInstallDir
}
else {
    $removeInstallDir = $false
}

function Assert-PathInsideTemp {
    param([string] $Path)

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedTemp = [System.IO.Path]::GetFullPath($env:TEMP)

    if (-not $resolvedPath.StartsWith($resolvedTemp, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove install smoke directory outside TEMP: $resolvedPath"
    }

    if ((Split-Path -Leaf $resolvedPath) -notlike "tes3mp-install-smoke-*") {
        throw "Refusing to remove unexpected install smoke directory: $resolvedPath"
    }
}

function Assert-InstalledFile {
    param([string] $Name)

    $path = Join-Path $InstallDir $Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installed runtime is missing $Name at $path"
    }
}

function Assert-NotInstalledFile {
    param([string] $Name)

    $path = Join-Path $InstallDir $Name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        throw "Installed runtime includes unexpected $Name at $path"
    }
}

function Assert-NotInstalledPath {
    param([string] $Name)

    $path = Join-Path $InstallDir $Name
    if (Test-Path -LiteralPath $path) {
        throw "Installed runtime includes unexpected $Name at $path"
    }
}

function Assert-NoInstalledServerSaveArtifacts {
    $dataRoot = Join-Path $InstallDir "server\data"
    $forbiddenExtensions = @(".db", ".txt", ".json", ".bak", ".tmp")
    $matches = @(Get-ChildItem -LiteralPath $dataRoot -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() })

    if ($matches.Count -gt 0) {
        throw "Installed runtime includes generated server save artifact $($matches[0].FullName)"
    }

    $saveRoot = Join-Path $dataRoot "saves"
    $unexpectedSaveDirectories = @(Get-ChildItem -LiteralPath $saveRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne "server" })
    if ($unexpectedSaveDirectories.Count -gt 0) {
        throw "Installed runtime includes generated server save directory $($unexpectedSaveDirectories[0].FullName)"
    }

    Assert-NotInstalledFile "server\data\saves\server\manifest.xml"
}

function Assert-LoginResources {
    $loginLayout = Join-Path $InstallDir "resources\vfs\mygui\login\communitymp_login.layout"
    $loginSkin = Join-Path $InstallDir "resources\vfs\mygui\login\communitymp_login.skin.xml"
    $loginResources = Join-Path $InstallDir "resources\vfs\mygui\login\communitymp_login.xml"
    $loginAshlandsHero = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp-ashlands-hero.jpg"
    $loginBackground = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp-causeway.jpg"
    $loginGathering = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp-gathering.jpg"
    $loginLogo = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp-logo.png"
    $loginServerHall = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp-server-hall.jpg"
    $loginAtmosphere = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp_login_atmosphere.png"
    $loginPanel = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp_login_panel.png"
    $loginButtonAtlas = Join-Path $InstallDir "resources\vfs\mygui\login\textures\communitymp_login_button_atlas.png"
    $loginMusic = Join-Path $InstallDir "resources\vfs\music\communitymp\nightinthedesertmix.ogg"
    $loginMusicCredits = Join-Path $InstallDir "resources\vfs\music\communitymp\nightinthedesertmix.CREDITS.txt"

    foreach ($requiredFile in @($loginLayout, $loginSkin, $loginResources, $loginAshlandsHero, $loginBackground, $loginGathering, $loginLogo, $loginServerHall, $loginAtmosphere, $loginPanel, $loginButtonAtlas, $loginMusic, $loginMusicCredits)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Installed runtime is missing CommunityMP login resource at $requiredFile"
        }
    }

    $layoutText = Get-Content -LiteralPath $loginLayout -Raw
    foreach ($requiredText in @("CommunityMP", "Server account", "Account username", "Account password", "Remember me", "Server join password")) {
        if ($layoutText -notmatch [regex]::Escape($requiredText)) {
            throw "Installed CommunityMP login layout is missing required text '$requiredText' at $loginLayout"
        }
    }
}

function Assert-ChatResources {
    foreach ($requiredFile in @(
        "resources\vfs\mygui\RussoOne-Regular.ttf",
        "resources\vfs\mygui\tes3mp_chat_font.xml",
        "resources\vfs\mygui\tes3mp_chat.skin.xml",
        "resources\vfs\mygui\tes3mp_chat.layout"
    )) {
        Assert-InstalledFile $requiredFile
    }

    $chatFont = Join-Path $InstallDir "resources\vfs\mygui\tes3mp_chat_font.xml"
    $fontText = Get-Content -LiteralPath $chatFont -Raw
    foreach ($requiredText in @('name="Russo"', 'RussoOne-Regular.ttf')) {
        if ($fontText -notmatch [regex]::Escape($requiredText)) {
            throw "Installed TES3MP chat font resource is missing required text '$requiredText' at $chatFont"
        }
    }
}

function Assert-OpenMwConfigBootstrap {
    $configFile = Join-Path $InstallDir "openmw.cfg"
    if (-not (Test-Path -LiteralPath $configFile -PathType Leaf)) {
        throw "Installed runtime is missing OpenMW config bootstrap at $configFile"
    }

    $configText = Get-Content -LiteralPath $configFile -Raw
    foreach ($requiredText in @("data-local=userdata/data", "user-data=userdata", "config=config", "resources=./resources", "data=./resources/vfs-mw")) {
        if ($configText -notmatch [regex]::Escape($requiredText)) {
            throw "Installed OpenMW config bootstrap is missing required text '$requiredText' at $configFile"
        }
    }

    foreach ($requiredDirectory in @("resources\vfs", "resources\vfs-mw")) {
        $path = Join-Path $InstallDir $requiredDirectory
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Installed OpenMW config bootstrap points at missing runtime directory $path"
        }
    }
}

function Assert-TextInBinaryFile {
    param(
        [string]$Path,
        [string]$Text
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $binaryText = [System.Text.Encoding]::ASCII.GetString($bytes)
    if (-not $binaryText.Contains($Text)) {
        throw "Executable $Path does not contain expected version text '$Text'"
    }
}

function Get-ExecutableVersionOutput {
    param([string]$Executable)

    $outputFile = Join-Path $env:TEMP ("tes3mp-version-" + [guid]::NewGuid().ToString("N") + ".txt")
    try {
        $command = '"' + $Executable + '" --version > "' + $outputFile + '" 2>&1'
        cmd.exe /d /c $command
        if ($LASTEXITCODE -ne 0) {
            $output = ""
            if (Test-Path -LiteralPath $outputFile -PathType Leaf) {
                $output = Get-Content -LiteralPath $outputFile -Raw
            }
            throw "communitymp-client.exe --version failed with exit code $LASTEXITCODE`n$output"
        }

        return @(Get-Content -LiteralPath $outputFile)
    } finally {
        Remove-Item -LiteralPath $outputFile -Force -ErrorAction SilentlyContinue
    }
}

function Get-CommunityMpLaunchTarget {
    param([string]$Mode)

    $communityExe = Join-Path $InstallDir "communitymp.exe"
    $outputFile = Join-Path $env:TEMP ("communitymp-target-" + [guid]::NewGuid().ToString("N") + ".txt")
    try {
        $command = '"' + $communityExe + '" --' + $Mode + ' --print-target > "' + $outputFile + '" 2>&1'
        cmd.exe /d /c $command
        if ($LASTEXITCODE -ne 0) {
            $output = ""
            if (Test-Path -LiteralPath $outputFile -PathType Leaf) {
                $output = Get-Content -LiteralPath $outputFile -Raw
            }
            throw "communitymp.exe --$Mode --print-target failed with exit code $LASTEXITCODE`n$output"
        }

        return (Get-Content -LiteralPath $outputFile | Select-Object -First 1).Trim()
    } finally {
        Remove-Item -LiteralPath $outputFile -Force -ErrorAction SilentlyContinue
    }
}

function Assert-CommunityMpLaunchProxy {
    Assert-InstalledFile "communitymp.exe"
    Assert-InstalledFile "CommunityMP Client.cmd"
    Assert-InstalledFile "CommunityMP Dedicated Server.cmd"
    Assert-InstalledFile "communitymp-client.sh"
    Assert-InstalledFile "communitymp-dedicated-server.sh"
    Assert-InstalledFile "CommunityMP Client.command"
    Assert-InstalledFile "CommunityMP Dedicated Server.command"

    $clientTarget = Get-CommunityMpLaunchTarget "client"
    if ((Split-Path -Leaf $clientTarget) -ne "communitymp-client.exe") {
        throw "communitymp.exe --client resolved unexpected target '$clientTarget'"
    }

    $serverTarget = Get-CommunityMpLaunchTarget "server"
    if ((Split-Path -Leaf $serverTarget) -ne "communitymp-server.exe") {
        throw "communitymp.exe --server resolved unexpected target '$serverTarget'"
    }
}

function Assert-VersionResourcesMatchExecutable {
    $versionFile = Join-Path $InstallDir "resources\version"
    $openmwExe = Join-Path $InstallDir "communitymp-client.exe"

    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
        throw "Installed runtime is missing resources/version at $versionFile"
    }

    if (-not (Test-Path -LiteralPath $openmwExe -PathType Leaf)) {
        throw "Installed runtime is missing communitymp-client.exe at $openmwExe"
    }

    $versionLines = @(Get-Content -LiteralPath $versionFile | ForEach-Object { $_.Trim() })
    if ($versionLines.Count -lt 3) {
        throw "Installed resources/version must contain version, commit hash, and tag hash at $versionFile"
    }

    $binaryVersion = Get-ExecutableVersionOutput $openmwExe

    $versionLine = $binaryVersion | Where-Object { $_ -match '^OpenMW version\s+(.+)$' } | Select-Object -First 1
    $revisionLine = $binaryVersion | Where-Object { $_ -match '^Revision:\s+(.+)$' } | Select-Object -First 1

    $binaryVersionValue = $null
    if ($versionLine -match '^OpenMW version\s+(.+)$') {
        $binaryVersionValue = $Matches[1]
    }
    if ($binaryVersionValue -ne $versionLines[0]) {
        throw "Installed resources/version version '$($versionLines[0])' does not match communitymp-client.exe --version output"
    }

    if (-not [string]::IsNullOrWhiteSpace($versionLines[1])) {
        $expectedShortRevision = $versionLines[1].Substring(0, [Math]::Min(10, $versionLines[1].Length))
        $binaryRevisionValue = $null
        if ($revisionLine -match '^Revision:\s+(.+)$') {
            $binaryRevisionValue = $Matches[1]
        }
        if ($binaryRevisionValue -ne $expectedShortRevision) {
            throw "Installed resources/version revision '$expectedShortRevision' does not match communitymp-client.exe --version output"
        }
    }

    Assert-TextInBinaryFile $openmwExe $versionLines[1]
    Assert-TextInBinaryFile $openmwExe $versionLines[2]
}

if (Test-Path -LiteralPath $InstallDir) {
    throw "InstallDir already exists: $InstallDir"
}

try {
    cmake --install $BuildDir --config $Configuration --prefix $InstallDir

    $requireFullDistribution = -not $Minimal

    Assert-InstalledFile "communitymp-client.exe"
    Assert-InstalledFile "communitymp-server.exe"
    Assert-InstalledFile "communitymp-client-default.cfg"
    Assert-InstalledFile "communitymp-server-default.cfg"
    Assert-InstalledFile "tes3mp-client-default.cfg"
    Assert-InstalledFile "tes3mp-server-default.cfg"
    Assert-NotInstalledFile "tes3mp.exe"
    Assert-NotInstalledFile "tes3mp-server.exe"
    Assert-InstalledFile "GameNetworkingSockets.dll"
    Assert-InstalledFile "LICENSE.txt"
    Assert-InstalledFile "server\LICENSE"
    Assert-InstalledFile "server\data\saves\server\security\banlist.xml"
    Assert-InstalledFile "server\data\saves\server\config\data-files.xml"
    Assert-NotInstalledFile "server\data\database.db"
    Assert-NotInstalledFile "server\data\banlist.json"
    Assert-NotInstalledFile "server\data\requiredDataFiles.json"
    Assert-NoInstalledServerSaveArtifacts
    Assert-OpenMwConfigBootstrap
    Assert-CommunityMpLaunchProxy
    Assert-LoginResources
    Assert-ChatResources
    Assert-VersionResourcesMatchExecutable

    if ($requireFullDistribution -or $WithLocalMaster) {
        Assert-InstalledFile "masterserver.exe"
        Assert-InstalledFile "communitymp-hub.exe"
        Assert-NotInstalledFile "tes3mp-browser.exe"
        Assert-NotInstalledFile "communitymp-browser.exe"
    }

    if ($requireFullDistribution) {
        foreach ($requiredFile in @(
            "openmw-cs.exe",
            "openmw-launcher.exe",
            "openmw-wizard.exe",
            "bsatool.exe",
            "esmtool.exe",
            "niftest.exe",
            "openmw-navmeshtool.exe",
            "openmw-bulletobjecttool.exe",
            "openmw-iniimporter.exe",
            "openmw-essimporter.exe",
            "defaults-cs.bin"
        )) {
            Assert-InstalledFile $requiredFile
        }
    }

    $smokeScript = Join-Path $PSScriptRoot "smoke-tes3mp-runtime.ps1"
    & $smokeScript -BuildDir $InstallDir -Configuration $Configuration -WithLocalMaster:$WithLocalMaster

    Write-Host "CommunityMP install smoke passed at $InstallDir"
}
finally {
    if ($removeInstallDir -and (Test-Path -LiteralPath $InstallDir)) {
        Assert-PathInsideTemp $InstallDir
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
    }
}
