[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingGuard
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required source file was not found: $path"
    }

    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

function Test-NotPattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if ([regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

$worldBindings = Get-SourceText "apps\openmw\mwlua\worldbindings.cpp"
$cellBindings = Get-SourceText "apps\openmw\mwlua\cellbindings.cpp"
$worldDocs = Get-SourceText "files\lua_api\openmw\world.lua"
$coreDocs = Get-SourceText "files\lua_api\openmw\core.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "OpenMW Lua docs expose world.cells as an ipairs-compatible list of Cell values" -Text $worldDocs `
    -Pattern 'List\s+of\s+all\s+cells.*@field\s+\[parent=#world\]\s+#list<openmw\.core#Cell>\s+cells.*for\s+i,\s*cell\s+in\s+ipairs\(world\.cells\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW Lua Cell docs preserve mixed ESM3/ESM4 identity fields" -Text $coreDocs `
    -Pattern '@type\s+Cell.*@field\s+#string\s+name\s+Name\s+of\s+the\s+cell.*@field\s+#string\s+id\s+Unique\s+record\s+ID\s+of\s+the\s+cell,.*formID\s+of\s+the\s+cell\s+for\s+ESM4\s+cells.*@field\s+#number\s+gridX.*@field\s+#number\s+gridY.*@field\s+#string\s+worldSpaceId' `
    -Missing $missing

Test-Pattern -Name "world.cells owns a dedicated read-only Cells usertype and publishes it on the world package" -Text $worldBindings `
    -Pattern 'struct\s+CellsStore.*is_automagical<MWLua::CellsStore>\s*:\s*std::false_type.*view\.new_usertype<CellsStore>\("Cells"\).*api\["cells"\]\s*=\s*CellsStore\{\}' `
    -Missing $missing

Test-Pattern -Name "world.cells captures the ESM3 and ESM4 cell stores separately" -Text $worldBindings `
    -Pattern 'const\s+MWWorld::Store<ESM::Cell>\*\s+cells3Store\s*=\s*&MWBase::Environment::get\(\)\.getESMStore\(\)->get<ESM::Cell>\(\);.*const\s+MWWorld::Store<ESM4::Cell>\*\s+cells4Store\s*=\s*&MWBase::Environment::get\(\)\.getESMStore\(\)->get<ESM4::Cell>\(\);' `
    -Missing $missing

Test-Pattern -Name "world.cells length is the combined ESM3 plus ESM4 cell count" -Text $worldBindings `
    -Pattern 'cells\[sol::meta_function::length\]\s*=\s*\[cells3Store,\s*cells4Store\]\(const\s+CellsStore&\)\s*\{\s*return\s+cells3Store->getSize\(\)\s*\+\s*cells4Store->getSize\(\);\s*\};' `
    -Missing $missing

Test-Pattern -Name "world.cells indexer bounds checks Lua's 1-based array range against the combined count" -Text $worldBindings `
    -Pattern 'const\s+std::size_t\s+cellCount\s*=\s*cells3Store->getSize\(\)\s*\+\s*cells4Store->getSize\(\);.*if\s*\(index\s*>\s*cellCount\s*\|\|\s*index\s*==\s*0\)\s*return\s+sol::nullopt;.*index--;\s*//\s*Translate\s+from\s+Lua''s\s+1-based\s+indexing\.' `
    -Missing $missing

Test-Pattern -Name "world.cells indexes ESM3 cells before the ESM4 tail" -Text $worldBindings `
    -Pattern 'if\s*\(index\s*<\s*cells3Store->getSize\(\)\)\s*\{.*const\s+ESM::Cell\*\s+cellRecord\s*=\s*cells3Store->at\(index\);.*getWorldModel\(\)->getCell\(\s*cellRecord->mId,\s*/\*forceLoad=\*/false\)' `
    -Missing $missing

Test-Pattern -Name "world.cells indexes ESM4 cells by subtracting only the ESM3 prefix count" -Text $worldBindings `
    -Pattern 'else\s*\{.*const\s+ESM4::Cell\*\s+cellRecord\s*=\s*cells4Store->at\(index\s*-\s*cells3Store->getSize\(\)\);.*getWorldModel\(\)->getCell\(\s*cellRecord->mId,\s*/\*forceLoad=\*/false\)' `
    -Missing $missing

Test-Pattern -Name "world.cells keeps pairs and ipairs on the OpenMW array iterator helper" -Text $worldBindings `
    -Pattern 'cells\[sol::meta_function::pairs\]\s*=\s*view\["ipairsForArray"\]\.template\s+get<sol::function>\(\);.*cells\[sol::meta_function::ipairs\]\s*=\s*view\["ipairsForArray"\]\.template\s+get<sol::function>\(\);' `
    -Missing $missing

Test-NotPattern -Name "world.cells must not use the ESM3 cell count twice for combined length" -Text $worldBindings `
    -Pattern 'return\s+cells3Store->getSize\(\)\s*\+\s*cells3Store->getSize\(\)' `
    -Missing $missing

Test-NotPattern -Name "world.cells must not index ESM4 cells by subtracting the ESM4 store size" -Text $worldBindings `
    -Pattern 'cells4Store->at\(index\s*-\s*cells4Store->getSize\(\)\)' `
    -Missing $missing

Test-Pattern -Name "Cell bindings expose stable identity, worldspace, grid, and display fields used by modern OpenMW Lua" -Text $cellBindings `
    -Pattern 'cellT\["name"\].*getNameId\(\).*cellT\["displayName"\].*translateCellName\(c\.mStore->getCell\(\)->getNameId\(\)\).*cellT\["id"\].*getId\(\).*cellT\["region"\].*getRegion\(\).*cellT\["worldSpaceId"\].*getWorldSpace\(\).*cellT\["gridX"\].*getGridX\(\).*cellT\["gridY"\].*getGridY\(\)' `
    -Missing $missing

Test-Pattern -Name "Global Cell:getAll loads unloaded cells and keeps ESM4 object categories visible to Lua" -Text $cellBindings `
    -Pattern 'if\s*\(cell\.mStore->getState\(\)\s*!=\s*MWWorld::CellStore::State_Loaded\)\s*cell\.mStore->load\(\);.*case\s+ESM::REC_ACTI4:.*forEachType<ESM4::Activator>.*case\s+ESM::REC_DOOR4:.*forEachType<ESM4::Door>.*case\s+ESM::REC_STAT4:.*forEachType<ESM4::Static>.*case\s+ESM::REC_WEAP4:.*forEachType<ESM4::Weapon>' `
    -Missing $missing

Write-Host "TES3MP OpenMW Lua world/cell sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 13"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
