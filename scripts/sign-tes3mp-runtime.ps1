param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir,
    [string]$SigningMode = $env:OPENACAI_SIGNING_MODE,
    [string]$Endpoint = $env:AZURE_ARTIFACT_SIGNING_ENDPOINT,
    [string]$AccountName = $env:AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME,
    [string]$CertificateProfileName = $env:AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE,
    [string]$CorrelationId = $env:AZURE_ARTIFACT_SIGNING_CORRELATION_ID,
    [string]$CertificateThumbprint = $env:CODESIGN_CERT_THUMBPRINT,
    [string]$CertificateStoreLocation = $env:CODESIGN_CERT_STORE_LOCATION,
    [string]$CertificateStoreName = $env:CODESIGN_CERT_STORE_NAME,
    [string]$SignToolPath = $env:SIGNTOOL_EXE,
    [string]$DlibPath = $env:AZURE_ARTIFACT_SIGNING_DLIB,
    [string]$TimestampUrl = "http://timestamp.acs.microsoft.com",
    [string]$Description = "CommunityMP",
    [string]$ToolsDir = "",
    [switch]$ExecutablesOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Join-Path $repoRoot ".tools\artifact-signing"
}

if ([string]::IsNullOrWhiteSpace($SigningMode)) {
    $SigningMode = "Auto"
}

if ([string]::IsNullOrWhiteSpace($CertificateStoreLocation)) {
    $CertificateStoreLocation = "CurrentUser"
}

if ([string]::IsNullOrWhiteSpace($CertificateStoreName)) {
    $CertificateStoreName = "My"
}

function Require-Value {
    param(
        [string]$Name,
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "Missing $Name. Set the matching environment variable or pass the parameter explicitly."
    }
}

function Normalize-Thumbprint {
    param([string]$Thumbprint)
    return ($Thumbprint -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
}

function Has-AzureSigningMetadata {
    return -not [string]::IsNullOrWhiteSpace($Endpoint) `
        -and -not [string]::IsNullOrWhiteSpace($AccountName) `
        -and -not [string]::IsNullOrWhiteSpace($CertificateProfileName)
}

function Resolve-SigningMode {
    switch ($SigningMode.ToLowerInvariant()) {
        "azure" {
            return "Azure"
        }
        "thumbprint" {
            return "Thumbprint"
        }
        "local" {
            return "Thumbprint"
        }
        "auto" {
            if (Has-AzureSigningMetadata) {
                return "Azure"
            }

            if (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
                return "Thumbprint"
            }

            throw "No signing configuration found. Set Azure Artifact Signing values or CODESIGN_CERT_THUMBPRINT."
        }
        default {
            throw "Unsupported signing mode '$SigningMode'. Use Auto, Azure, or Thumbprint."
        }
    }
}

function Get-LatestSignTool {
    if (-not [string]::IsNullOrWhiteSpace($SignToolPath)) {
        if (-not (Test-Path -LiteralPath $SignToolPath -PathType Leaf)) {
            throw "SIGNTOOL_EXE does not exist: $SignToolPath"
        }

        return (Resolve-Path -LiteralPath $SignToolPath).Path
    }

    $windowsKitBin = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (-not (Test-Path -LiteralPath $windowsKitBin -PathType Container)) {
        throw "Windows SDK SignTool was not found. Install Windows SDK Build Tools or set SIGNTOOL_EXE."
    }

    $candidate = Get-ChildItem -LiteralPath $windowsKitBin -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
        Sort-Object @{ Expression = {
            try {
                [version]$_.Directory.Parent.Name
            } catch {
                [version]"0.0.0.0"
            }
        }; Descending = $true } |
        Select-Object -First 1

    if (-not $candidate) {
        throw "x64 SignTool was not found. Install Windows SDK Build Tools or set SIGNTOOL_EXE."
    }

    return $candidate.FullName
}

function Ensure-NuGet {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $nugetPath = Join-Path $ToolsDir "nuget.exe"

    if (-not (Test-Path -LiteralPath $nugetPath -PathType Leaf)) {
        Write-Host "Downloading NuGet CLI for local Artifact Signing client restore..."
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nugetPath
    }

    return $nugetPath
}

function Get-ArtifactSigningDlib {
    if (-not [string]::IsNullOrWhiteSpace($DlibPath)) {
        if (-not (Test-Path -LiteralPath $DlibPath -PathType Leaf)) {
            throw "AZURE_ARTIFACT_SIGNING_DLIB does not exist: $DlibPath"
        }

        return (Resolve-Path -LiteralPath $DlibPath).Path
    }

    $candidateRoots = @($ToolsDir)

    $userNuGetPackages = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.artifactsigning.client"
    if (Test-Path -LiteralPath $userNuGetPackages -PathType Container) {
        $candidateRoots += $userNuGetPackages
    }

    foreach ($root in ($candidateRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }

        $existing = Get-ChildItem -LiteralPath $root -Recurse -Filter "Azure.CodeSigning.Dlib.dll" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\bin\\x64\\Azure\.CodeSigning\.Dlib\.dll$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if ($existing) {
            return $existing.FullName
        }
    }

    $nugetPath = Ensure-NuGet
    Write-Host "Restoring Microsoft.ArtifactSigning.Client locally..."
    & $nugetPath install Microsoft.ArtifactSigning.Client -x -OutputDirectory $ToolsDir -NonInteractive | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw "NuGet failed to restore Microsoft.ArtifactSigning.Client."
    }

    $restored = Get-ChildItem -LiteralPath $ToolsDir -Recurse -Filter "Azure.CodeSigning.Dlib.dll" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\bin\\x64\\Azure\.CodeSigning\.Dlib\.dll$" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if (-not $restored) {
        throw "Azure.CodeSigning.Dlib.dll was not found after restoring Microsoft.ArtifactSigning.Client."
    }

    return $restored.FullName
}

function New-MetadataFile {
    param([string]$MetadataPath)

    $metadata = [ordered]@{
        Endpoint = $Endpoint.Trim()
        CodeSigningAccountName = $AccountName.Trim()
        CertificateProfileName = $CertificateProfileName.Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($CorrelationId)) {
        $metadata.CorrelationId = $CorrelationId.Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($env:AZURE_ARTIFACT_SIGNING_EXCLUDE_CREDENTIALS)) {
        $metadata.ExcludeCredentials = @(
            $env:AZURE_ARTIFACT_SIGNING_EXCLUDE_CREDENTIALS.Split(",") |
                ForEach-Object { $_.Trim() } |
                Where-Object { $_ }
        )
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $MetadataPath) | Out-Null
    $metadataJson = $metadata | ConvertTo-Json -Depth 5
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($MetadataPath, $metadataJson, $utf8NoBom)
}

function Find-LocalCertificate {
    Require-Value "CODESIGN_CERT_THUMBPRINT" $CertificateThumbprint
    $thumbprint = Normalize-Thumbprint $CertificateThumbprint
    $locations = if ($CertificateStoreLocation -eq "Auto") {
        @("CurrentUser", "LocalMachine")
    } else {
        @($CertificateStoreLocation)
    }
    $searchedStores = New-Object System.Collections.Generic.List[string]

    foreach ($location in $locations) {
        $storePath = "Cert:\$location\$CertificateStoreName"
        $null = $searchedStores.Add("$location\$CertificateStoreName")
        if (-not (Test-Path -LiteralPath $storePath)) {
            continue
        }

        $certificate = Get-ChildItem -LiteralPath $storePath |
            Where-Object { (Normalize-Thumbprint $_.Thumbprint) -eq $thumbprint } |
            Select-Object -First 1

        if ($certificate) {
            return [pscustomobject]@{
                Certificate = $certificate
                Location = $location
                StoreName = $CertificateStoreName
            }
        }
    }

    throw "Certificate thumbprint $thumbprint was not found in $($searchedStores -join ', ')."
}

function Get-SignableFiles {
    param([string]$Root)

    $patterns = if ($ExecutablesOnly) {
        @("*.exe")
    } else {
        @("*.exe", "*.dll")
    }

    $files = New-Object System.Collections.Generic.List[string]
    foreach ($pattern in $patterns) {
        Get-ChildItem -LiteralPath $Root -Recurse -Filter $pattern -File |
            ForEach-Object { $null = $files.Add($_.FullName) }
    }

    return @($files | Sort-Object -Unique)
}

function Get-SigningPlan {
    param([string[]]$Files)

    $toSign = New-Object System.Collections.Generic.List[string]
    $skipped = New-Object System.Collections.Generic.List[object]
    foreach ($file in $Files) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if (-not $Force -and $signature.Status -eq "Valid") {
            $null = $skipped.Add([pscustomobject]@{
                Path = $file
                Status = $signature.Status
                Signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { "" }
            })
            continue
        }

        $null = $toSign.Add($file)
    }

    return [pscustomobject]@{
        ToSign = $toSign.ToArray()
        Skipped = $skipped.ToArray()
    }
}

function Invoke-SignTool {
    param(
        [string]$SignTool,
        [string[]]$Arguments,
        [string]$ErrorMessage
    )

    & $SignTool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw $ErrorMessage
    }
}

$installRoot = [System.IO.Path]::GetFullPath($InstallDir)
if (-not (Test-Path -LiteralPath $installRoot -PathType Container)) {
    throw "CommunityMP runtime directory was not found: $installRoot"
}

$resolvedSigningMode = Resolve-SigningMode
$signTool = Get-LatestSignTool
$dlib = $null
$metadataPath = $null
$localCertificate = $null

if ($resolvedSigningMode -eq "Azure") {
    Require-Value "AZURE_ARTIFACT_SIGNING_ENDPOINT" $Endpoint
    Require-Value "AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME" $AccountName
    Require-Value "AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE" $CertificateProfileName

    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    $net8Runtime = if ($dotnet) { dotnet --list-runtimes | Select-String -Pattern "^Microsoft\.NETCore\.App 8\." } else { $null }
    if (-not $net8Runtime) {
        Write-Warning "Artifact Signing Client Tools require the .NET 8 runtime. Install it if signing fails during dlib load."
    }

    $dlib = Get-ArtifactSigningDlib
    $metadataPath = Join-Path $ToolsDir "metadata.tes3mp.generated.json"
    New-MetadataFile -MetadataPath $metadataPath
} else {
    $localCertificate = Find-LocalCertificate
    if (-not $localCertificate.Certificate.HasPrivateKey) {
        throw "Certificate $($localCertificate.Certificate.Thumbprint) was found in $($localCertificate.Location)\$($localCertificate.StoreName), but it does not expose a private key for signing."
    }
}

$allFiles = Get-SignableFiles $installRoot
if ($allFiles.Count -eq 0) {
    throw "No signable files were found under $installRoot."
}

$plan = Get-SigningPlan $allFiles
Write-Host "CommunityMP runtime signing mode: $resolvedSigningMode"
Write-Host "Signable files found: $($allFiles.Count)"
Write-Host "Already valid signatures skipped: $($plan.Skipped.Count)"
Write-Host "Files to sign: $($plan.ToSign.Count)"

foreach ($file in $plan.ToSign) {
    Write-Host "Signing $file"
    if ($resolvedSigningMode -eq "Azure") {
        Invoke-SignTool -SignTool $signTool -Arguments @("sign", "/v", "/debug", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256", "/d", $Description, "/dlib", $dlib, "/dmdf", $metadataPath, $file) -ErrorMessage "SignTool failed while signing $file"
    } else {
        $thumbprint = Normalize-Thumbprint $CertificateThumbprint
        $signArgs = @("sign", "/v", "/debug", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256", "/d", $Description, "/s", $localCertificate.StoreName, "/sha1", $thumbprint)
        if ($localCertificate.Location -eq "LocalMachine") {
            $signArgs += "/sm"
        }

        $signArgs += $file
        Invoke-SignTool -SignTool $signTool -Arguments $signArgs -ErrorMessage "SignTool failed while signing $file"
    }

    Invoke-SignTool -SignTool $signTool -Arguments @("verify", "/pa", "/v", $file) -ErrorMessage "SignTool verification failed for $file"
}

$invalid = Get-SignableFiles $installRoot |
    ForEach-Object {
        $signature = Get-AuthenticodeSignature -LiteralPath $_
        if ($signature.Status -ne "Valid") {
            [pscustomobject]@{
                Path = $_
                Status = $signature.Status
            }
        }
    }

if ($invalid) {
    $invalid | Format-Table -AutoSize | Out-String | Write-Host
    throw "One or more package executables or DLLs do not have a valid Authenticode signature."
}

Write-Host "CommunityMP runtime signatures are valid."
