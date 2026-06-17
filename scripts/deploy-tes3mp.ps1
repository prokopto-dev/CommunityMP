param(
    [string]$BuildDir = "build\tes3mp-openmw",
    [string]$InstallDir = "",
    [string]$Configuration = "RelWithDebInfo",
    [switch]$NoBuild,
    [switch]$Minimal,
    [Alias("IncludeBrowser")]
    [switch]$IncludeHub,
    [switch]$IncludeMaster,
    [switch]$StopRunning,
    [switch]$StartServer
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

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Copy-FileIfPresent {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Source -PathType Container) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
    }
}

function Copy-FirstMatchingFile {
    param(
        [string[]]$Directories,
        [string]$FileName,
        [string]$Destination
    )

    foreach ($directory in $Directories) {
        if ([string]::IsNullOrWhiteSpace($directory)) {
            continue
        }

        $source = Join-Path $directory $FileName
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $Destination -Force
            return $true
        }
    }

    return $false
}

function Assert-LoginResources {
    param([string]$Root)

    $loginLayout = Join-Path $Root "resources\vfs\mygui\login\communitymp_login.layout"
    $loginSkin = Join-Path $Root "resources\vfs\mygui\login\communitymp_login.skin.xml"
    $loginResources = Join-Path $Root "resources\vfs\mygui\login\communitymp_login.xml"
    $loginAshlandsHero = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp-ashlands-hero.jpg"
    $loginBackground = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp-causeway.jpg"
    $loginGathering = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp-gathering.jpg"
    $loginLogo = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp-logo.png"
    $loginServerHall = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp-server-hall.jpg"
    $loginAtmosphere = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp_login_atmosphere.png"
    $loginPanel = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp_login_panel.png"
    $loginButtonAtlas = Join-Path $Root "resources\vfs\mygui\login\textures\communitymp_login_button_atlas.png"
    $loginMusic = Join-Path $Root "resources\vfs\music\communitymp\nightinthedesertmix.ogg"
    $loginMusicCredits = Join-Path $Root "resources\vfs\music\communitymp\nightinthedesertmix.CREDITS.txt"

    foreach ($requiredFile in @($loginLayout, $loginSkin, $loginResources, $loginAshlandsHero, $loginBackground, $loginGathering, $loginLogo, $loginServerHall, $loginAtmosphere, $loginPanel, $loginButtonAtlas, $loginMusic, $loginMusicCredits)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Runtime is missing CommunityMP login resource at $requiredFile"
        }
    }

    $layoutText = Get-Content -LiteralPath $loginLayout -Raw
    foreach ($requiredText in @("CommunityMP", "Server account", "Account username", "Account password", "Remember me", "Server join password")) {
        if ($layoutText -notmatch [regex]::Escape($requiredText)) {
            throw "CommunityMP login layout is missing required text '$requiredText' at $loginLayout"
        }
    }
}

function Assert-OpenMwConfigBootstrap {
    param([string]$Root)

    $configFile = Join-Path $Root "openmw.cfg"
    if (-not (Test-Path -LiteralPath $configFile -PathType Leaf)) {
        throw "Runtime is missing OpenMW config bootstrap at $configFile"
    }

    $configText = Get-Content -LiteralPath $configFile -Raw
    foreach ($requiredText in @("data-local=userdata/data", "user-data=userdata", "config=config", "resources=./resources", "data=./resources/vfs-mw")) {
        if ($configText -notmatch [regex]::Escape($requiredText)) {
            throw "OpenMW config bootstrap is missing required text '$requiredText' at $configFile"
        }
    }

    foreach ($requiredDirectory in @("resources\vfs", "resources\vfs-mw")) {
        $path = Join-Path $Root $requiredDirectory
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "OpenMW config bootstrap points at missing runtime directory $path"
        }
    }
}

function Get-OpenMwUserConfigCandidates {
    $candidates = @()

    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $candidates += Join-Path $env:USERPROFILE "Documents\My Games\OpenMW\openmw.cfg"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $candidates += Join-Path $env:LOCALAPPDATA "OpenMW\openmw.cfg"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:APPDATA)) {
        $candidates += Join-Path $env:APPDATA "OpenMW\openmw.cfg"
    }

    return @($candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -Unique)
}

function Copy-OpenMwContentBootstrap {
    param([string]$Root)

    $portableConfigDir = Join-Path $Root "config"
    $portableConfigFile = Join-Path $portableConfigDir "openmw.cfg"
    $contentKeysPattern = "^\s*(fallback-archive|data|content|groundcover|encoding)\s*="

    foreach ($candidate in Get-OpenMwUserConfigCandidates) {
        $contentLines = @(Get-Content -LiteralPath $candidate | Where-Object { $_ -match $contentKeysPattern })
        $hasContent = @($contentLines | Where-Object { $_ -match "^\s*content\s*=" }).Count -gt 0
        $hasData = @($contentLines | Where-Object { $_ -match "^\s*data\s*=" }).Count -gt 0

        if (-not $hasContent -or -not $hasData) {
            continue
        }

        New-Item -ItemType Directory -Force -Path $portableConfigDir | Out-Null
        $lines = @(
            "# Local game-data bootstrap copied during CommunityMP packaging.",
            "# Edit these paths if this portable folder is moved to a machine with a different Morrowind install.",
            ""
        ) + $contentLines
        Set-Content -LiteralPath $portableConfigFile -Value $lines -Encoding ASCII
        Write-Host "Installed portable OpenMW content bootstrap from $candidate"
        return
    }

    if (-not (Test-Path -LiteralPath $portableConfigFile -PathType Leaf)) {
        New-Item -ItemType Directory -Force -Path $portableConfigDir | Out-Null
        Set-Content -LiteralPath $portableConfigFile -Encoding ASCII -Value @(
            "# Add Morrowind data/content entries here if the client exits with 'No content file given'.",
            "# Example:",
            "# data=`"C:\Path\To\Morrowind\Data Files`"",
            "# fallback-archive=Morrowind.bsa",
            "# fallback-archive=Tribunal.bsa",
            "# fallback-archive=Bloodmoon.bsa",
            "# content=Morrowind.esm",
            "# content=Tribunal.esm",
            "# content=Bloodmoon.esm"
        )
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

function Assert-VersionResourcesMatchExecutable {
    param([string]$Root)

    $versionFile = Join-Path $Root "resources\version"
    $openmwExe = Join-Path $Root "communitymp-client.exe"

    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
        throw "Runtime is missing resources/version at $versionFile"
    }

    if (-not (Test-Path -LiteralPath $openmwExe -PathType Leaf)) {
        throw "Runtime is missing communitymp-client.exe at $openmwExe"
    }

    $versionLines = @(Get-Content -LiteralPath $versionFile | ForEach-Object { $_.Trim() })
    if ($versionLines.Count -lt 3) {
        throw "resources/version must contain version, commit hash, and tag hash at $versionFile"
    }

    $binaryVersion = Get-ExecutableVersionOutput $openmwExe

    $versionLine = $binaryVersion | Where-Object { $_ -match '^OpenMW version\s+(.+)$' } | Select-Object -First 1
    $revisionLine = $binaryVersion | Where-Object { $_ -match '^Revision:\s+(.+)$' } | Select-Object -First 1

    $binaryVersionValue = $null
    if ($versionLine -match '^OpenMW version\s+(.+)$') {
        $binaryVersionValue = $Matches[1]
    }
    if ($binaryVersionValue -ne $versionLines[0]) {
        throw "resources/version version '$($versionLines[0])' does not match communitymp-client.exe --version output"
    }

    if (-not [string]::IsNullOrWhiteSpace($versionLines[1])) {
        $expectedShortRevision = $versionLines[1].Substring(0, [Math]::Min(10, $versionLines[1].Length))
        $binaryRevisionValue = $null
        if ($revisionLine -match '^Revision:\s+(.+)$') {
            $binaryRevisionValue = $Matches[1]
        }
        if ($binaryRevisionValue -ne $expectedShortRevision) {
            throw "resources/version revision '$expectedShortRevision' does not match communitymp-client.exe --version output"
        }
    }

    Assert-TextInBinaryFile $openmwExe $versionLines[1]
    Assert-TextInBinaryFile $openmwExe $versionLines[2]
}

function Copy-MsvcRuntimeDependencies {
    param([string]$Destination)

    $redistRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
        $redistRoots += Join-Path $env:VCINSTALLDIR "Redist\MSVC"
    }

    $redistRoots += @(
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
    )

    $candidateDirs = @()
    foreach ($root in ($redistRoots | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $candidateDirs += Get-ChildItem -LiteralPath $root -Directory |
                Sort-Object LastWriteTime -Descending |
                ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Container }
        }
    }

    $candidateDirs += "C:\Windows\System32"
    $candidateDirs = $candidateDirs | Select-Object -Unique

    foreach ($fileName in @(
        "concrt140.dll",
        "msvcp140.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "msvcp140_atomic_wait.dll",
        "msvcp140_codecvt_ids.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "vcruntime140_threads.dll"
    )) {
        if (-not (Copy-FirstMatchingFile $candidateDirs $fileName (Join-Path $Destination $fileName))) {
            Write-Warning "Could not find optional VC runtime dependency: $fileName"
        }
    }
}

function Copy-UcrtRuntimeDependencies {
    param([string]$Destination)

    $ucrtDirs = @()
    if (-not [string]::IsNullOrWhiteSpace($env:WindowsSdkDir)) {
        $ucrtDirs += Join-Path $env:WindowsSdkDir "Redist\ucrt\DLLs\x64"
    }

    $ucrtDirs += @(
        "C:\Program Files (x86)\Windows Kits\10\Redist\ucrt\DLLs\x64",
        "C:\Program Files\Windows Kits\10\Redist\ucrt\DLLs\x64"
    )

    $ucrtDir = $ucrtDirs |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
        Select-Object -First 1

    if ($ucrtDir) {
        Get-ChildItem -LiteralPath $ucrtDir -Filter "*.dll" -File |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name) -Force
            }
        return
    }

    $systemDir = "C:\Windows\System32"
    foreach ($fileName in @(
        "api-ms-win-crt-conio-l1-1-0.dll",
        "api-ms-win-crt-convert-l1-1-0.dll",
        "api-ms-win-crt-environment-l1-1-0.dll",
        "api-ms-win-crt-filesystem-l1-1-0.dll",
        "api-ms-win-crt-heap-l1-1-0.dll",
        "api-ms-win-crt-locale-l1-1-0.dll",
        "api-ms-win-crt-math-l1-1-0.dll",
        "api-ms-win-crt-runtime-l1-1-0.dll",
        "api-ms-win-crt-stdio-l1-1-0.dll",
        "api-ms-win-crt-string-l1-1-0.dll",
        "api-ms-win-crt-time-l1-1-0.dll",
        "ucrtbase.dll"
    )) {
        if (-not (Copy-FirstMatchingFile @($systemDir) $fileName (Join-Path $Destination $fileName))) {
            Write-Warning "Could not find optional UCRT dependency: $fileName"
        }
    }
}

function Get-Sha256Hash {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            return -join ($sha256.ComputeHash($stream) | ForEach-Object { $_.ToString("X2") })
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$repoRoot = (Get-GitOutput @("rev-parse", "--show-toplevel")).Trim()
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    throw "Pass -InstallDir with the TES3MP runtime directory to update."
}

$buildRoot = Resolve-RepoPath $BuildDir
$installRoot = [System.IO.Path]::GetFullPath($InstallDir)

if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory not found: $buildRoot"
}

$includeHub = -not $Minimal -or $IncludeHub
$includeMaster = -not $Minimal -or $IncludeMaster
$includeTools = -not $Minimal

if (-not $NoBuild) {
    $targets = @("communitymp", "openmw", "tes3mp-server")
    if ($includeHub) {
        $targets += "communitymp-hub"
    }
    if ($includeMaster) {
        $targets += "masterserver"
    }
    if ($includeTools) {
        $targets += @(
            "openmw-cs",
            "openmw-launcher",
            "openmw-wizard",
            "bsatool",
            "esmtool",
            "niftest",
            "openmw-navmeshtool",
            "openmw-bulletobjecttool",
            "openmw-iniimporter",
            "openmw-essimporter"
        )
    }

    $buildArgs = @(
        "--build", $buildRoot,
        "--config", $Configuration,
        "--target"
    )
    $buildArgs += $targets

    Invoke-Checked cmake $buildArgs
}

$runtimeRoot = Join-Path $buildRoot $Configuration
if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) {
    $runtimeRoot = $buildRoot
}

$exeNames = @("communitymp.exe", "communitymp-client.exe", "communitymp-server.exe")
$obsoleteExeNames = @("tes3mp.exe", "tes3mp-server.exe")
if ($includeHub) {
    $exeNames += "communitymp-hub.exe"
    $obsoleteExeNames += "tes3mp-browser.exe"
    $obsoleteExeNames += "communitymp-browser.exe"
}
if ($includeMaster) {
    $exeNames += "masterserver.exe"
}
if ($includeTools) {
    $exeNames += @(
        "openmw-cs.exe",
        "openmw-launcher.exe",
        "openmw-wizard.exe",
        "bsatool.exe",
        "esmtool.exe",
        "niftest.exe",
        "openmw-navmeshtool.exe",
        "openmw-bulletobjecttool.exe",
        "openmw-iniimporter.exe",
        "openmw-essimporter.exe"
    )
}

$targetExePaths = ($exeNames + $obsoleteExeNames) | ForEach-Object { Join-Path $installRoot $_ }
$running = Get-CimInstance Win32_Process |
    Where-Object { $targetExePaths -contains $_.ExecutablePath } |
    Select-Object ProcessId, Name, ExecutablePath

if ($running) {
    if (-not $StopRunning) {
        $details = ($running | ForEach-Object { "$($_.Name) pid $($_.ProcessId)" }) -join ", "
        throw "Runtime executable is running ($details). Close it or pass -StopRunning."
    }

    foreach ($process in $running) {
        Stop-Process -Id $process.ProcessId -Force
    }
}

New-Item -ItemType Directory -Force -Path $installRoot | Out-Null

foreach ($obsoleteExeName in $obsoleteExeNames) {
    Remove-Item -LiteralPath (Join-Path $installRoot $obsoleteExeName) -Force -ErrorAction SilentlyContinue
}

foreach ($exeName in $exeNames) {
    $source = Join-Path $runtimeRoot $exeName
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Build artifact not found: $source"
    }

    Copy-Item -LiteralPath $source -Destination (Join-Path $installRoot $exeName) -Force
}

$launcherFiles = @(
    "CommunityMP Client.cmd",
    "CommunityMP Dedicated Server.cmd",
    "communitymp-client.sh",
    "communitymp-dedicated-server.sh",
    "CommunityMP Client.command",
    "CommunityMP Dedicated Server.command"
)

foreach ($launcherFile in $launcherFiles) {
    Copy-FileIfPresent (Join-Path $runtimeRoot $launcherFile) (Join-Path $installRoot $launcherFile)
}

Get-ChildItem -LiteralPath $runtimeRoot -Filter "*.dll" -File |
    Where-Object { $_.Name -notmatch "_d\.dll$" } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $installRoot $_.Name) -Force
    }

Copy-MsvcRuntimeDependencies $installRoot
Copy-UcrtRuntimeDependencies $installRoot

foreach ($directoryName in @("iconengines", "imageformats", "platforms", "styles")) {
    Copy-DirectoryContents (Join-Path $runtimeRoot $directoryName) (Join-Path $installRoot $directoryName)
}

Get-ChildItem -LiteralPath $runtimeRoot -Directory -Filter "osgPlugins-*" |
    ForEach-Object {
        Copy-DirectoryContents $_.FullName (Join-Path $installRoot $_.Name)
    }

foreach ($fileName in @(
    "defaults.bin",
    "defaults-cs.bin",
    "gamecontrollerdb.txt",
    "communitymp-client-default.cfg",
    "communitymp-server-default.cfg",
    "tes3mp-client-default.cfg",
    "tes3mp-server-default.cfg"
)) {
    Copy-FileIfPresent (Join-Path $runtimeRoot $fileName) (Join-Path $installRoot $fileName)
}

if ($includeTools) {
    Copy-FileIfPresent (Join-Path $repoRoot "files\openmw-cs.cfg") (Join-Path $installRoot "openmw-cs.cfg")
}

$openmwConfigSource = Join-Path $runtimeRoot "openmw.cfg.install"
if (-not (Test-Path -LiteralPath $openmwConfigSource -PathType Leaf)) {
    $openmwConfigSource = Join-Path $runtimeRoot "openmw.cfg"
}
if (-not (Test-Path -LiteralPath $openmwConfigSource -PathType Leaf)) {
    throw "Build artifact not found: openmw.cfg.install"
}
Copy-Item -LiteralPath $openmwConfigSource -Destination (Join-Path $installRoot "openmw.cfg") -Force
foreach ($portableDirectory in @("config", "userdata", "userdata\data")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $installRoot $portableDirectory) | Out-Null
}
Copy-OpenMwContentBootstrap $installRoot

Copy-DirectoryContents (Join-Path $runtimeRoot "resources") (Join-Path $installRoot "resources")
Copy-DirectoryContents (Join-Path $runtimeRoot "server\lib") (Join-Path $installRoot "server\lib")
Copy-DirectoryContents (Join-Path $runtimeRoot "server\scripts") (Join-Path $installRoot "server\scripts")
Assert-OpenMwConfigBootstrap $installRoot
Assert-LoginResources $installRoot
Assert-VersionResourcesMatchExecutable $installRoot

foreach ($fileName in @("LICENSE", "README.md", "Tutorial.md")) {
    Copy-FileIfPresent (Join-Path $runtimeRoot "server\$fileName") (Join-Path $installRoot "server\$fileName")
}

foreach ($dirName in @("cell", "custom", "map", "player", "recordstore", "world")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $installRoot "server\data\$dirName") | Out-Null
}

foreach ($dirName in @("saves\server\security", "saves\server\config")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $installRoot "server\data\$dirName") | Out-Null
}

foreach ($fileName in @("saves\server\security\banlist.xml", "saves\server\config\data-files.xml")) {
    $destination = Join-Path $installRoot "server\data\$fileName"
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Copy-FileIfPresent (Join-Path $runtimeRoot "server\data\$fileName") $destination
    }
}

$versionFile = Join-Path $installRoot "resources\version"
if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
    Write-Host "Installed resources/version:"
    Get-Content -LiteralPath $versionFile
}

Write-Host "Installed executable hashes:"
$exeNames |
    ForEach-Object {
        $path = Join-Path $installRoot $_
        [pscustomobject]@{
            Path = $path
            Hash = Get-Sha256Hash $path
        }
    } |
    Format-Table -AutoSize

if ($StartServer) {
    $serverExe = Join-Path $installRoot "communitymp.exe"
    Start-Process -FilePath $serverExe -ArgumentList "--server" -WorkingDirectory $installRoot -WindowStyle Hidden
}
