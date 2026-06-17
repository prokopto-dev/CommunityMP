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

    if (-not (Test-RegexPattern -Text $Text -Pattern $Pattern)) {
        $Missing.Add($Name)
    }
}

function Test-AbsentPattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    if (Test-RegexPattern -Text $Text -Pattern $Pattern) {
        $Missing.Add($Name)
    }
}

function Invoke-RegexIsMatch {
    param(
        [string]$Text,
        [string]$Pattern
    )

    try {
        return [regex]::IsMatch(
            $Text,
            $Pattern,
            [System.Text.RegularExpressions.RegexOptions]::Singleline,
            [TimeSpan]::FromSeconds(2))
    } catch [System.Text.RegularExpressions.RegexMatchTimeoutException] {
        return $null
    }
}

function Split-RegexOrderedSegments {
    param([string]$Pattern)

    $segments = [System.Collections.Generic.List[string]]::new()
    $builder = [System.Text.StringBuilder]::new()
    $escaped = $false
    $inClass = $false

    for ($i = 0; $i -lt $Pattern.Length; ++$i) {
        $c = $Pattern[$i]

        if ($escaped) {
            [void]$builder.Append($c)
            $escaped = $false
            continue
        }

        if ($c -eq '\') {
            [void]$builder.Append($c)
            $escaped = $true
            continue
        }

        if ($c -eq '[') {
            $inClass = $true
            [void]$builder.Append($c)
            continue
        }

        if ($c -eq ']' -and $inClass) {
            $inClass = $false
            [void]$builder.Append($c)
            continue
        }

        if (-not $inClass -and $c -eq '.' -and $i + 1 -lt $Pattern.Length -and $Pattern[$i + 1] -eq '*') {
            $segment = $builder.ToString()
            if ($segment -ne "") {
                $segments.Add($segment)
            }
            [void]$builder.Clear()
            ++$i
            continue
        }

        [void]$builder.Append($c)
    }

    $tail = $builder.ToString()
    if ($tail -ne "") {
        $segments.Add($tail)
    }

    return $segments
}

function Test-OrderedRegexPattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    $segments = @(Split-RegexOrderedSegments $Pattern)
    if ($segments.Count -eq 0) {
        return $true
    }

    $offset = 0

    foreach ($segment in $segments) {
        $cleanSegment = $segment
        if ($cleanSegment.StartsWith("^")) {
            $cleanSegment = $cleanSegment.Substring(1)
        }
        if ($cleanSegment.EndsWith("$")) {
            $cleanSegment = $cleanSegment.Substring(0, $cleanSegment.Length - 1)
        }
        if ($cleanSegment -eq "") {
            continue
        }

        $remaining = $Text.Substring($offset)
        try {
            $match = [regex]::Match(
                $remaining,
                $cleanSegment,
                [System.Text.RegularExpressions.RegexOptions]::Singleline,
                [TimeSpan]::FromSeconds(2))
        } catch [System.Text.RegularExpressions.RegexMatchTimeoutException] {
            return $false
        }

        if (-not $match.Success) {
            return $false
        }

        $offset += $match.Index + [Math]::Max($match.Length, 1)
        if ($offset -gt $Text.Length) {
            $offset = $Text.Length
        }
    }

    return $true
}

function Find-LookaheadClose {
    param(
        [string]$Pattern,
        [int]$Start
    )

    $escaped = $false
    $inClass = $false
    $depth = 0

    for ($i = $Start; $i -lt $Pattern.Length; ++$i) {
        $c = $Pattern[$i]

        if ($escaped) {
            $escaped = $false
            continue
        }

        if ($c -eq '\') {
            $escaped = $true
            continue
        }

        if ($c -eq '[') {
            $inClass = $true
            continue
        }

        if ($c -eq ']' -and $inClass) {
            $inClass = $false
            continue
        }

        if ($inClass) {
            continue
        }

        if ($c -eq '(') {
            ++$depth
            continue
        }

        if ($c -eq ')') {
            if ($depth -eq 0) {
                return $i
            }

            --$depth
        }
    }

    return -1
}

function Get-LookaheadParts {
    param([string]$Pattern)

    $positive = [System.Collections.Generic.List[string]]::new()
    $negative = [System.Collections.Generic.List[string]]::new()
    $index = 0

    if ($Pattern.StartsWith("^")) {
        $index = 1
    }

    while ($index -lt $Pattern.Length) {
        if ($index + 5 -le $Pattern.Length -and $Pattern.Substring($index, 5) -eq "(?=.*") {
            $contentStart = $index + 5
            $close = Find-LookaheadClose -Pattern $Pattern -Start $contentStart
            if ($close -lt 0) {
                return $null
            }
            $positive.Add($Pattern.Substring($contentStart, $close - $contentStart))
            $index = $close + 1
            continue
        }

        if ($index + 5 -le $Pattern.Length -and $Pattern.Substring($index, 5) -eq "(?!.*") {
            $contentStart = $index + 5
            $close = Find-LookaheadClose -Pattern $Pattern -Start $contentStart
            if ($close -lt 0) {
                return $null
            }
            $negative.Add($Pattern.Substring($contentStart, $close - $contentStart))
            $index = $close + 1
            continue
        }

        $remaining = $Pattern.Substring($index)
        if ($remaining -eq "" -or $remaining -eq ".*" -or $remaining -eq ".*$") {
            break
        }

        return $null
    }

    if ($positive.Count -eq 0 -and $negative.Count -eq 0) {
        return $null
    }

    return [pscustomobject]@{
        Positive = $positive
        Negative = $negative
    }
}

function Test-LookaheadPattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    $parts = Get-LookaheadParts $Pattern
    if ($null -eq $parts) {
        return $null
    }

    foreach ($patternPart in $parts.Negative) {
        if (Test-OrderedRegexPattern -Text $Text -Pattern $patternPart) {
            return $false
        }
    }

    foreach ($patternPart in $parts.Positive) {
        if (-not (Test-OrderedRegexPattern -Text $Text -Pattern $patternPart)) {
            return $false
        }
    }

    return $true
}

function Test-RegexPattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    $direct = Invoke-RegexIsMatch -Text $Text -Pattern $Pattern
    if ($null -ne $direct) {
        return $direct
    }

    $lookahead = Test-LookaheadPattern -Text $Text -Pattern $Pattern
    if ($null -ne $lookahead) {
        return $lookahead
    }

    return Test-OrderedRegexPattern -Text $Text -Pattern $Pattern
}

$mechanicsHelper = Get-SourceText "apps\openmw\mwmp\MechanicsHelper.cpp"
$mechanicsHeader = Get-SourceText "apps\openmw\mwmp\MechanicsHelper.hpp"
$character = Get-SourceText "apps\openmw\mwmechanics\character.cpp"
$characterHeader = Get-SourceText "apps\openmw\mwmechanics\character.hpp"
$actors = Get-SourceText "apps\openmw\mwmechanics\actors.cpp"
$actorsHeader = Get-SourceText "apps\openmw\mwmechanics\actors.hpp"
$mechanicsManager = Get-SourceText "apps\openmw\mwbase\mechanicsmanager.hpp"
$mechanicsManagerImp = Get-SourceText "apps\openmw\mwmechanics\mechanicsmanagerimp.cpp"
$mechanicsManagerImpHeader = Get-SourceText "apps\openmw\mwmechanics\mechanicsmanagerimp.hpp"
$npc = Get-SourceText "apps\openmw\mwclass\npc.cpp"
$creature = Get-SourceText "apps\openmw\mwclass\creature.cpp"
$combat = Get-SourceText "apps\openmw\mwmechanics\combat.cpp"
$worldImp = Get-SourceText "apps\openmw\mwworld\worldimp.cpp"
$localPlayer = Get-SourceText "apps\openmw\mwmp\LocalPlayer.cpp"
$localActor = Get-SourceText "apps\openmw\mwmp\LocalActor.cpp"
$dedicatedPlayerHeader = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.hpp"
$dedicatedPlayer = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.cpp"
$dedicatedActor = Get-SourceText "apps\openmw\mwmp\DedicatedActor.cpp"
$actorList = Get-SourceText "apps\openmw\mwmp\ActorList.cpp"
$objectList = Get-SourceText "apps\openmw\mwmp\ObjectList.cpp"
$baseStructs = Get-SourceText "components\openmw-mp\Base\BaseStructs.hpp"
$basePlayer = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$baseActor = Get-SourceText "components\openmw-mp\Base\BaseActor.hpp"
$actorStatsAuthority = Get-SourceText "components\openmw-mp\Base\ActorStatsAuthority.hpp"
$playerAnimPlayPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAnimPlay.cpp"
$playerAttackPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerAttack.cpp"
$playerCastPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerCast.cpp"
$playerDeathPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerDeath.cpp"
$playerStatsDynamicPacket = Get-SourceText "components\openmw-mp\Packets\Player\PacketPlayerStatsDynamic.cpp"
$actorAttackPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorAttack.cpp"
$actorCastPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorCast.cpp"
$actorDeathPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorDeath.cpp"
$actorStatsDynamicPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorStatsDynamic.cpp"
$actorStatsDynamicProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorStatsDynamic.hpp"
$actorDeathProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorDeath.hpp"
$actorAttackProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorAttack.hpp"
$actorCastProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorCast.hpp"
$actorSequenceCoalescing = Get-SourceText "apps\openmw-mp\processors\actor\ActorSequenceCoalescing.hpp"
$scriptFunctions = Get-SourceText "apps\openmw-mp\Script\ScriptFunctions.hpp"
$basePacketTest = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$serverLuaCompatTest = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"
$playerAttackProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAttack.hpp"
$playerCastProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerCast.hpp"
$clientPlayerStatsDynamicProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerStatsDynamic.hpp"
$animation = Get-SourceText "apps\openmw\mwrender\animation.cpp"
$animBlendController = Get-SourceText "apps\openmw\mwrender\animblendcontroller.cpp"
$serverPlayer = Get-SourceText "apps\openmw-mp\Player.cpp"
$serverPlayerHeader = Get-SourceText "apps\openmw-mp\Player.hpp"
$serverPlayerMovementSnapshot = Get-SourceText "apps\openmw-mp\processors\player\PlayerMovementSnapshot.hpp"
$serverPlayerAnimPlayProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerAnimPlay.hpp"
$serverPlayerAttackProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerAttack.hpp"
$serverPlayerCastProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerCast.hpp"
$serverPlayerDeathProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerDeath.hpp"
$serverPlayerStatsDynamicProcessor = Get-SourceText "apps\openmw-mp\processors\player\ProcessorPlayerStatsDynamic.hpp"
$serverSimulation = Get-SourceText "apps\openmw-mp\ServerSimulation.cpp"
$serverCell = Get-SourceText "apps\openmw-mp\Cell.cpp"
$statsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Stats.cpp"
$actorsFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Actors.cpp"
$dialogueFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Dialogue.cpp"
$clientCell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$clientCellController = Get-SourceText "apps\openmw\mwmp\CellController.cpp"
$clientPlayerAnimPlayProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAnimPlay.hpp"
$clientPlayerDeathProcessor = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerDeath.hpp"
$packetReader = Get-SourceText "files\tes3mp\server\scripts\packetReader.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$serverCore = Get-SourceText "files\tes3mp\server\scripts\serverCore.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$cellBase = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$playerBase = Get-SourceText "files\tes3mp\server\scripts\player\base.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Local melee and ranged attacks serialize actual local hit results" -Text ($mechanicsHeader + "`n" + $character + "`n" + $npc + "`n" + $creature + "`n" + $combat) `
    -Pattern 'queueLocalAttackStart\(const\s+MWWorld::Ptr&\s+attacker,\s*bool\s+isRanged,\s*const\s+std::string&\s+attackAnimation\).*queueLocalMeleeAttack\(const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim,\s*const\s+MWWorld::Ptr&\s+weapon,\s*float\s+attackStrength,\s*int\s+attackType,\s*bool\s+isHit,\s*bool\s+success,\s*float\s+damage,\s*bool\s+block,\s*const\s+osg::Vec3f&\s+hitPosition,\s*bool\s+applyWeaponEnchantment\).*queueLocalRangedAttack\(const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim,\s*const\s+MWWorld::Ptr&\s+weapon,\s*const\s+MWWorld::Ptr&\s+projectile,\s*float\s+attackStrength,\s*bool\s+isHit,\s*bool\s+success,\s*float\s+damage,\s*const\s+osg::Vec3f&\s+hitPosition,\s*bool\s+applyWeaponEnchantment,\s*bool\s+applyAmmoEnchantment\).*queueLocalAttackStart\(.*AttackWindUp.*queueLocalMeleeAttack\(.*weapon.*blocked.*appliedWeaponEnchantment.*queueLocalRangedAttack\(.*appliedEnchantment' `
    -Missing $missing

Test-Pattern -Name "Attack replay validates packet data and gates serialized damage behind authoritative mode" -Text $mechanicsHelper `
    -Pattern 'bool\s+isFiniteProjectileOrigin\(const\s+mwmp::ProjectileOrigin&\s+projectileOrigin\).*void\s+MechanicsHelper::processAttack\(Attack\s+attack,\s*const\s+MWWorld::Ptr&\s+attacker,\s*bool\s+applyAuthoritativeState\).*Ignoring attack with invalid type.*Ignoring attack hit with invalid damage or hit position.*Ignoring ranged attack with invalid projectile origin.*if\s*\(attack\.isHit\).*if\s*\(victim\.isEmpty\(\)\).*Ignoring attack hit with unresolved target.*if\s*\(!applyAuthoritativeState\)\s*return;.*if\s*\(attack\.success\s*&&\s*attack\.applyWeaponEnchantment\).*if\s*\(isRanged\s*&&\s*!ammoPtr\.isEmpty\(\)\s*&&\s*attack\.success\s*&&\s*attack\.applyAmmoEnchantment\).*float\s+damage\s*=\s*attack\.success\s*\?\s*attack\.damage\s*:\s*0\.f;.*if\s*\(!isRanged\s*&&\s*attack\.block\).*setBlock\(true\);.*damage\s*=\s*0;.*damage\s*=\s*applyAttackDamageModifiers\(damage,\s*attacker,\s*victim,\s*isHealthDamage\);.*damages\[isHealthDamage\s*\?\s*"health"\s*:\s*"fatigue"\]\s*=\s*damage;.*victim\.getClass\(\)\.onHit\(victim,\s*damages,\s*object,\s*attacker,\s*attack\.success' `
    -Missing $missing

Test-Pattern -Name "Remote combat target resolution falls back from actor records to mapped or unique cell actors" -Text $mechanicsHelper `
    -Pattern 'bool\s+hasUsableRefNum\(unsigned\s+int\s+refNum\).*refNum\s*!=\s*0\s*&&\s*refNum\s*!=\s*static_cast<unsigned\s+int>\(-1\);.*bool\s+isUsableActorTarget\(const\s+MWWorld::Ptr&\s+ptr,\s*const\s+mwmp::Target&\s+target\).*ptr\.getClass\(\)\.isActor\(\).*ptr\.getRefData\(\)\.isEnabled\(\).*ptr\.getCellRef\(\)\.getCount\(false\)\s*!=\s*0.*MWWorld::Ptr\s+findUniqueActorByRefId\(MWWorld::CellStore\*\s+cellStore,\s*const\s+mwmp::Target&\s+target\).*bool\s+ambiguous\s*=\s*false;.*multiple matching actors.*void\s+registerTargetServerId\(const\s+MWWorld::Ptr&\s+ptr,\s*const\s+mwmp::Target&\s+target\).*registerServerObjectId\(ptr,\s*target\.mpNum\);.*MWWorld::Ptr\s+resolveActorTargetInCell\(const\s+mwmp::Target&\s+target,\s*MWWorld::CellStore\*\s+fallbackCellStore\).*getLocalRefNumForServerMpNum\(target\.mpNum\).*findActorByLocalRefNum\(fallbackCellStore,\s*\*localRefNum,\s*target\).*findActorByLocalRefNum\(fallbackCellStore,\s*target\.refNum,\s*target\).*if\s*\(!target\.refId\.empty\(\)\).*findUniqueActorByRefId\(fallbackCellStore,\s*target\).*std::string\s+describeTarget\(const\s+mwmp::Target&\s+target\).*packetGuidToString\(target\.guid\).*refId=.*MWWorld::Ptr\s+resolveTargetPtr\(const\s+mwmp::Target&\s+target,\s*MWWorld::CellStore\*\s+fallbackCellStore\s*=\s*nullptr\).*return\s+resolveActorTargetInCell\(target,\s*fallbackCellStore\);.*MWWorld::Ptr\s+victim\s*=\s*resolveTargetPtr\(attack\.target,\s*attacker\.getCell\(\)\);.*MWWorld::Ptr\s+victim;.*victim\s*=\s*resolveTargetPtr\(cast\.target,\s*caster\.getCell\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Melee attack packets carry weapon context independent of equipment channel ordering" -Text ($mechanicsHelper + "`n" + $playerAttackPacket + "`n" + $actorAttackPacket + "`n" + $basePacketTest) `
    -Pattern 'void\s+MechanicsHelper::queueLocalMeleeAttack\(const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim,\s*const\s+MWWorld::Ptr&\s+weapon.*if\s*\(!weapon\.isEmpty\(\)\)\s*attack->rangedWeaponId\s*=\s*weapon\.getCellRef\(\)\.getRefId\(\)\.serializeText\(\);.*const\s+ESM::RefId\s+attackWeaponId\s*=\s*stringRefId\(attack\.rangedWeaponId\);.*if\s*\(!attackWeaponId\.empty\(\)\).*weaponPtr\s*=\s*inventoryStore\.search\(attackWeaponId\);.*Ignoring attack weapon.*if\s*\(player->attack\.type\s*==\s*mwmp::Attack::MELEE\).*player->attack\.attackAnimation.*player->attack\.rangedWeaponId.*if\s*\(actor\.attack\.type\s*==\s*mwmp::Attack::MELEE\).*actor\.attack\.attackAnimation.*actor\.attack\.rangedWeaponId.*playerMeleeAttackRoundTripsWeaponContext.*actorMeleeAttackRoundTripsWeaponContext' `
    -Missing $missing

Test-Pattern -Name "Melee attack packets carry release strength and reset stale attack fields" -Text ($mechanicsHelper + "`n" + $playerAttackPacket + "`n" + $actorAttackPacket) `
    -Pattern 'void\s+MechanicsHelper::resetAttack\(Attack\*\s+attack\).*attack->type\s*=\s*Attack::MELEE;.*attack->attackAnimation\.clear\(\);.*attack->rangedWeaponId\.clear\(\);.*attack->rangedAmmoId\.clear\(\);.*attack->damage\s*=\s*0\.f;.*attack->attackStrength\s*=\s*0\.f;.*attack->pressed\s*=\s*false;.*attack->instant\s*=\s*false;.*if\s*\(player->attack\.type\s*==\s*mwmp::Attack::MELEE\).*RW\(player->attack\.attackAnimation,\s*send,\s*true\).*RW\(player->attack\.attackStrength,\s*send\).*RW\(player->attack\.rangedWeaponId,\s*send,\s*true\).*if\s*\(actor\.attack\.type\s*==\s*mwmp::Attack::MELEE\).*RW\(actor\.attack\.attackAnimation,\s*send,\s*true\).*RW\(actor\.attack\.attackStrength,\s*send\).*RW\(actor\.attack\.rangedWeaponId,\s*send,\s*true\)' `
    -Missing $missing

Test-Pattern -Name "Combat event packets reject truncated attack cast and death payloads" -Text ($playerAttackPacket + "`n" + $playerCastPacket + "`n" + $playerDeathPacket + "`n" + $actorAttackPacket + "`n" + $actorCastPacket + "`n" + $actorDeathPacket + "`n" + $basePacketTest) `
    -Pattern 'PacketPlayerAttack::Packet.*if\s*\(!RW\(player->combatSequence,\s*send\)\).*return;.*if\s*\(!RW\(player->positionSequence,\s*send\).*return;.*if\s*\(!RW\(player->attack\.target\.isPlayer,\s*send\)\).*return;.*if\s*\(!RW\(player->attack\.damage,\s*send\).*return;.*PacketPlayerCast::Packet.*if\s*\(!RW\(player->combatSequence,\s*send\)\).*return;.*if\s*\(!RW\(player->cast\.target\.isPlayer,\s*send\)\).*return;.*if\s*\(!RW\(player->cast\.hasProjectile,\s*send\)\).*return;.*PacketPlayerDeath::Packet.*if\s*\(!RW\(player->combatSequence,\s*send\)\).*return;.*if\s*\(!RW\(player->deathState,\s*send\).*return;.*PacketActorAttack::Actor.*if\s*\(!RW\(actor\.combatSequence,\s*send\)\).*return;.*if\s*\(!RW\(actor\.attack\.target\.isPlayer,\s*send\)\).*return;.*if\s*\(!RW\(actor\.attack\.damage,\s*send\).*return;.*PacketActorCast::Actor.*if\s*\(!RW\(actor\.combatSequence,\s*send\)\).*return;.*if\s*\(!RW\(actor\.cast\.target\.isPlayer,\s*send\)\).*return;.*PacketActorDeath::Actor.*if\s*\(!RW\(actor\.deathState,\s*send\).*return;.*playerCombatEventPacketsRejectTruncatedPayloads.*actorCombatEventPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Player combat events are sequenced before server fanout and client replay" -Text ($basePlayer + "`n" + $playerAnimPlayPacket + "`n" + $playerAttackPacket + "`n" + $playerCastPacket + "`n" + $playerDeathPacket + "`n" + $localPlayer + "`n" + $dialogueFunctions + "`n" + $serverPlayerMovementSnapshot + "`n" + $serverPlayerAnimPlayProcessor + "`n" + $serverPlayerAttackProcessor + "`n" + $serverPlayerCastProcessor + "`n" + $serverPlayerDeathProcessor + "`n" + $playerAttackProcessor + "`n" + $playerCastProcessor + "`n" + $clientPlayerAnimPlayProcessor + "`n" + $clientPlayerDeathProcessor + "`n" + $basePacketTest) `
    -Pattern '(?=.*isNewerPlayerCombatSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);)(?=.*std::uint32_t\s+combatSequence\s*=\s*0;)(?=.*std::uint32_t\s+acceptedCombatSequence\s*=\s*0;)(?=.*bool\s+hasAcceptedCombatPacket\s*=\s*false;)(?=.*void\s+advanceCombatSequence\(\).*\+\+combatSequence;)(?=.*bool\s+isCombatPacketSequenceAllowed\(\)\s+const.*isNewerPlayerCombatSequence\(combatSequence,\s*acceptedCombatSequence\))(?=.*void\s+acceptCurrentCombatPacket\(\).*acceptedCombatSequence\s*=\s*combatSequence;.*hasAcceptedCombatPacket\s*=\s*true;)(?=.*bool\s+acceptCombatPacket\(\).*if\s*\(!isCombatPacketSequenceAllowed\(\)\).*combatSequence\s*=\s*acceptedCombatSequence;)(?=.*PacketPlayerAnimPlay::Packet.*RW\(player->combatSequence,\s*send\))(?=.*PacketPlayerAttack::Packet.*RW\(player->combatSequence,\s*send\))(?=.*PacketPlayerCast::Packet.*RW\(player->combatSequence,\s*send\))(?=.*PacketPlayerDeath::Packet.*RW\(player->combatSequence,\s*send\))(?=.*void\s+LocalPlayer::updateAttackOrCast\(\).*if\s*\(attackReady\).*advanceCombatSequence\(\);.*acceptCurrentCombatPacket\(\);.*ID_PLAYER_ATTACK.*if\s*\(cast\.shouldSend\).*advanceCombatSequence\(\);.*acceptCurrentCombatPacket\(\);.*ID_PLAYER_CAST)(?=.*void\s+LocalPlayer::sendDeath\(char\s+newDeathState\).*advanceCombatSequence\(\);.*acceptCurrentCombatPacket\(\);.*ID_PLAYER_DEATH)(?=.*void\s+DialogueFunctions::PlayAnimation\(unsigned\s+short\s+pid.*advanceCombatSequence\(\);.*acceptCurrentCombatPacket\(\);.*ID_PLAYER_ANIM_PLAY)(?=.*acceptSequencedPlayerCombatEvent\(Player&\s+player\).*isCombatPacketSequenceAllowed\(\).*normalizePlayerMovementSnapshot\(player\).*acceptCurrentCombatPacket\(\);)(?=.*ProcessorPlayerAnimPlay.*player\.creatureStats\.mDead.*acceptSequencedPlayerCombatEvent\(player\))(?=.*ProcessorPlayerAttack.*player\.creatureStats\.mDead.*acceptSequencedPlayerCombatEvent\(player\))(?=.*ProcessorPlayerCast.*player\.creatureStats\.mDead.*acceptSequencedPlayerCombatEvent\(player\))(?=.*ProcessorPlayerDeath.*acceptSequencedPlayerCombatEvent\(player\))(?=.*ProcessorPlayerAttack.*isCombatPacketSequenceAllowed\(\).*normalizePositionPacket\(\).*acceptCurrentCombatPacket\(\).*MechanicsHelper::processAttack)(?=.*ProcessorPlayerCast.*isCombatPacketSequenceAllowed\(\).*normalizePositionPacket\(\).*acceptCurrentCombatPacket\(\).*MechanicsHelper::processCast)(?=.*ProcessorPlayerAnimPlay.*acceptCombatPacket\(\).*playAnimation\(\).*isCombatPacketSequenceAllowed\(\).*normalizePositionPacket\(\).*acceptCurrentCombatPacket\(\).*dedicatedPlayer\.playAnimation\(\))(?=.*ProcessorPlayerDeath.*acceptCombatPacket\(\).*static_cast<LocalPlayer\*>\(player\)->die\(\).*isCombatPacketSequenceAllowed\(\).*normalizePositionPacket\(\).*acceptCurrentCombatPacket\(\).*dedicatedPlayer\.die\(\))(?=.*playerCombatSequenceRejectsStaleEvents)' `
    -Missing $missing

Test-Pattern -Name "Combat event packets reject invalid values before processor fanout" -Text ($playerAttackPacket + "`n" + $playerCastPacket + "`n" + $actorAttackPacket + "`n" + $actorCastPacket + "`n" + $basePacketTest) `
    -Pattern '(?=.*PacketPlayerAttack::Packet.*!isValidAttackType\(player->attack\.type\).*packetValid\s*=\s*false)(?=.*PacketActorAttack::Actor.*!isValidAttackType\(actor\.attack\.type\).*packetValid\s*=\s*false)(?=.*!std::isfinite\(player->attack\.damage\).*player->attack\.damage\s*<\s*0\.f.*!isFinitePosition\(player->attack\.hitPosition\).*packetValid\s*=\s*false)(?=.*!std::isfinite\(actor\.attack\.damage\).*actor\.attack\.damage\s*<\s*0\.f.*!isFinitePosition\(actor\.attack\.hitPosition\).*packetValid\s*=\s*false)(?=.*PacketPlayerCast::Packet.*!isValidCastType\(player->cast\.type\).*packetValid\s*=\s*false.*player->cast\.spellId\.empty\(\).*packetValid\s*=\s*false)(?=.*PacketActorCast::Actor.*!isValidCastType\(actor\.cast\.type\).*packetValid\s*=\s*false.*actor\.cast\.spellId\.empty\(\).*packetValid\s*=\s*false)(?=.*playerCombatEventPacketsRejectInvalidValues)(?=.*actorCombatEventPacketsRejectInvalidValues)' `
    -Missing $missing

Test-Pattern -Name "Remote attack replay finalizes OpenMW armor and player/NPC difficulty modifiers synchronously" -Text $mechanicsHelper `
    -Pattern 'float\s+applyAttackDamageModifiers\(float\s+damage,\s*const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim,\s*bool\s+isHealthDamage\).*getArmorRating\(victim,\s*true\).*fCombatArmorMinMult.*damage\s*=\s*std::max\(damage,\s*1\.f\);.*attackerIsPlayer\s*==\s*victimIsPlayer.*Settings::game\(\)\.mDifficulty.*fDifficultyMult.*return\s+std::max\(0\.f,\s*damage\s*\*\s*\(1\.f\s*\+\s*difficultyScale\)\);' `
    -Missing $missing

Test-Pattern -Name "Remote attack replay forces visible hit reaction from serialized result" -Text $mechanicsHelper `
    -Pattern 'void\s+applyAttackReaction\(const\s+mwmp::Attack&\s+attack,\s*const\s+MWWorld::Ptr&\s+victim,\s*float\s+appliedDamage\).*if\s*\(victim\.isEmpty\(\)\s*\|\|\s*!attack\.success\).*MWMechanics::CreatureStats&\s+victimStats\s*=\s*victim\.getClass\(\)\.getCreatureStats\(victim\);.*if\s*\(!attack\.block\s*&&\s*attack\.knockdown\).*victimStats\.setHitRecovery\(false\);.*victimStats\.setKnockedDown\(true\);.*else\s+if\s*\(!attack\.block\s*&&\s*appliedDamage\s*>=\s*0\.001f\s*&&\s*!victimStats\.getKnockedDown\(\)\).*victimStats\.setHitRecovery\(true\);.*victim\.getClass\(\)\.onHit\(victim,\s*damages,\s*object,\s*attacker,\s*attack\.success.*applyAttackReaction\(attack,\s*victim,\s*damage\);' `
    -Missing $missing

Test-Pattern -Name "Remote attack press replays visible weapon wind-up immediately" -Text ($mechanicsHelper + "`n" + $character + "`n" + $characterHeader + "`n" + $actors + "`n" + $actorsHeader + "`n" + $mechanicsManager + "`n" + $mechanicsManagerImp + "`n" + $mechanicsManagerImpHeader) `
    -Pattern '(?=.*virtual\s+void\s+replayAttackStart\(const\s+MWWorld::Ptr&\s+ptr,\s*std::string_view\s+attackType\))(?=.*void\s+MechanicsManager::replayAttackStart\(const\s+MWWorld::Ptr&\s+ptr,\s*std::string_view\s+attackType\).*mActors\.replayAttackStart\(ptr,\s*attackType\);)(?=.*void\s+Actors::replayAttackStart\(const\s+MWWorld::Ptr&\s+ptr,\s*std::string_view\s+attackType\).*getCharacterController\(\)\.replayAttackStart\(attackType\);)(?=.*void\s+CharacterController::replayAttackStart\(std::string_view\s+attackType\).*if\s*\(!mAnimation\)\s*return;.*if\s*\(attackType\.empty\(\)\)\s*attackType\s*=\s*mAttackType;.*if\s*\(attackType\.empty\(\)\)\s*return;.*mAttackType\s*=\s*attackType;.*setAttackType\(attackType\);.*setAttackingOrSpell\(true\);.*updateWeaponState\(\);)(?=.*void\s+MechanicsHelper::processAttack\(Attack\s+attack,\s*const\s+MWWorld::Ptr&\s+attacker,\s*bool\s+applyAuthoritativeState\).*if\s*\(attack\.pressed\).*attackerStats\.setAttackingOrSpell\(true\);.*replayAttackStart\(attacker,\s*attackType\);.*Failed to replay attack start)' `
    -Missing $missing

Test-Pattern -Name "Remote melee release replays visible weapon animation instead of leaving remote players in wind-up" -Text ($mechanicsHelper + "`n" + $character + "`n" + $characterHeader + "`n" + $actors + "`n" + $mechanicsManager + "`n" + $mechanicsManagerImp) `
    -Pattern '(?=.*virtual\s+void\s+replayAttackRelease\(.*const\s+MWWorld::Ptr&\s+ptr,\s*std::string_view\s+attackType,\s*float\s+attackStrength\))(?=.*void\s+MechanicsManager::replayAttackRelease\(.*mActors\.replayAttackRelease\(ptr,\s*attackType,\s*attackStrength\);)(?=.*void\s+Actors::replayAttackRelease\(.*getCharacterController\(\)\.replayAttackRelease\(attackType,\s*attackStrength\);)(?=.*bool\s+isMeleeAttackType\(std::string_view\s+attackType\).*"chop".*"slash".*"thrust")(?=.*std::string_view\s+resolveRemoteAttackType\(const\s+Attack&\s+attack,\s*const\s+MWMechanics::CreatureStats&\s+attackerStats\).*attack\.type\s*==\s*Attack::RANGED.*return\s+"shoot";.*attack\.type\s*!=\s*Attack::MELEE.*return\s+\{\};.*isMeleeAttackType\(attack\.attackAnimation\).*return\s+attack\.attackAnimation;.*lastAttackType\s*=\s*attackerStats\.getAttackType\(\);.*isMeleeAttackType\(lastAttackType\).*return\s+lastAttackType;.*return\s+"chop";)(?=.*void\s+CharacterController::replayAttackRelease\(std::string_view\s+attackType,\s*float\s+attackStrength\).*if\s*\(!mAnimation\)\s*return;.*if\s*\(attackType\.empty\(\)\)\s*attackType\s*=\s*mAttackType;.*if\s*\(attackType\.empty\(\)\)\s*return;.*mAttackType\s*=\s*attackType;.*setAttackType\(attackType\).*mUpperBodyState\s*<=\s*UpperBodyState::WeaponEquipped.*mUpperBodyState\s*==\s*UpperBodyState::Equipping.*setAttackingOrSpell\(true\);.*updateWeaponState\(\);.*mUpperBodyState\s*==\s*UpperBodyState::Equipping.*mAnimation->disable\(mCurrentWeapon\);.*UpperBodyState::WeaponEquipped.*if\s*\(mUpperBodyState\s*==\s*UpperBodyState::WeaponEquipped\)\s*updateWeaponState\(\);.*mAttackStrength\s*=\s*std::clamp\(attackStrength,\s*0\.f,\s*1\.f\);.*setAttackingOrSpell\(false\);.*updateWeaponState\(\);)(?=.*float\s+getVisibleRemoteAttackStrength\(float\s+attackStrength\).*attackStrength\s*<=\s*0\.f.*return\s+0\.45f;.*return\s+std::clamp\(attackStrength,\s*0\.f,\s*1\.f\);)(?=.*void\s+MechanicsHelper::processAttack\(Attack\s+attack,\s*const\s+MWWorld::Ptr&\s+attacker,\s*bool\s+applyAuthoritativeState\).*std::string_view\s+attackType\s*=\s*resolveRemoteAttackType\(attack,\s*attackerStats\);.*attackerStats\.setDrawState\(MWMechanics::DrawState::Weapon\);.*attackerStats\.setAttackType\(attackType\);.*if\s*\(attack\.pressed\).*attackerStats\.setAttackingOrSpell\(true\);.*else.*replayAttackStrength\s*=\s*getVisibleRemoteAttackStrength\(attack\.attackStrength\);.*try.*replayAttackRelease\(.*attacker,\s*attackType,\s*replayAttackStrength\);.*catch\s*\(const\s+std::exception&\s+e\).*Failed to replay attack release.*attackerStats\.setAttackingOrSpell\(false\);)' `
    -Missing $missing

Test-Pattern -Name "Stale multiplayer actor lookups drop packet work instead of throwing map access" -Text ($clientCell + "`n" + $clientCellController + "`n" + $mechanicsHelper + "`n" + $dedicatedActor + "`n" + $character) `
    -Pattern 'LocalActor\s+\*Cell::getLocalActor\(std::string\s+actorIndex\).*localActors\.find\(actorIndex\).*found\s*==\s*localActors\.end\(\).*return\s+nullptr;.*DedicatedActor\s+\*Cell::getDedicatedActor\(std::string\s+actorIndex\).*dedicatedActors\.find\(actorIndex\).*found\s*==\s*dedicatedActors\.end\(\).*return\s+nullptr;.*LocalActor\s+\*CellController::getLocalActor\(MWWorld::Ptr\s+ptr\).*ptr\.mRef\s*==\s*nullptr.*return\s+nullptr;.*localActorsToCells\.find\(actorIndex\).*cellsInitialized\.find\(actorRecord->second\).*return\s+cell->second->getLocalActor\(actorIndex\);.*DedicatedActor\s+\*CellController::getDedicatedActor\(MWWorld::Ptr\s+ptr\).*ptr\.mRef\s*==\s*nullptr.*return\s+nullptr;.*dedicatedActorsToCells\.find\(actorIndex\).*cellsInitialized\.find\(actorRecord->second\).*return\s+cell->second->getDedicatedActor\(actorIndex\);.*if\s*\(mwmp::LocalActor\*\s+localActor\s*=\s*mwmp::Main::get\(\)\.getCellController\(\)->getLocalActor\(ptr\)\).*return\s+&localActor->attack;.*if\s*\(mwmp::DedicatedActor\*\s+dedicatedActor\s*=\s*mwmp::Main::get\(\)\.getCellController\(\)->getDedicatedActor\(ptr\)\).*return\s+&dedicatedActor->attack;.*if\s*\(mwmp::LocalActor\*\s+localActor\s*=\s*cellController->getLocalActor\(aiTarget\.refNum,\s*aiTarget\.mpNum\)\).*targetPtr\s*=\s*localActor->getPtr\(\);.*if\s*\(mwmp::LocalActor\*\s+localActor\s*=\s*mwmp::Main::get\(\)\.getCellController\(\)->getLocalActor\(mPtr\)\).*localActor->sendDeath' `
    -Missing $missing

Test-Pattern -Name "Stale multiplayer cell lookups drop proxy migration instead of throwing map access" -Text ($clientCell + "`n" + $clientCellController + "`n" + $dedicatedPlayer + "`n" + $objectList) `
    -Pattern '(?=.*Cell\s+\*CellController::getCell\(const\s+ESM::Cell&\s+cell\).*cellsInitialized\.find\(cellDescription\(cell\)\).*found\s*==\s*cellsInitialized\.end\(\).*return\s+nullptr;.*return\s+found->second;)(?=.*Cell\*\s+mpCell\s*=\s*getCell\(cell\).*mpCell\s*!=\s*nullptr\s*&&\s*mpCell->hasLocalAuthority\(\))(?=.*if\s*\(Cell\*\s+mpCell\s*=\s*Main::get\(\)\.getCellController\(\)->getCell\(cell\)\).*mpCell->updateLocal\(true\);)(?=.*if\s*\(mwmp::Cell\*\s+mpCell\s*=\s*mwmp::Main::get\(\)\.getCellController\(\)->getCell\(cellStore->getCell\(\)->getEsm3\(\)\)\).*mpCell->initializeLocalActor\(ptrFound\);)(?=.*Cell\s+\*newCell\s*=\s*cellController->getCell\(actor->cell\).*if\s*\(newCell\s*!=\s*nullptr\).*newCell->localActors\[mapIndex\])(?=.*Cell\s+\*newCell\s*=\s*cellController->getCell\(dedicatedActor->cell\).*if\s*\(newCell\s*!=\s*nullptr\).*newCell->dedicatedActors\[mapIndex\].*Destination dedicated actor cell is no longer initialized)' `
    -Missing $missing

Test-Pattern -Name "Animation blending skips missing nodes and bone transforms instead of throwing map access" -Text ($animation + "`n" + $animBlendController) `
    -Pattern 'auto\s+controller\s*=\s*blendControllers\.find\(node\);.*if\s*\(controller\s*!=\s*blendControllers\.end\(\)\).*animController\s*=\s*controller->second;.*const\s+NodeMap&\s+nodeMap\s*=\s*getNodeMap\(\);.*NodeMap::const_iterator\s+found\s*=\s*nodeMap\.find\(it->first\);.*if\s*\(found\s*==\s*nodeMap\.end\(\)\).*Skipping animation controller for missing node.*continue;.*osg::ref_ptr<osg::Node>\s+node\s*=\s*found->second;.*auto\s+blendBoneTransform\s*=\s*mBlendBoneTransforms\.find\(bone\);.*if\s*\(blendBoneTransform\s*==\s*mBlendBoneTransforms\.end\(\)\)\s*return;.*const\s+osg::Matrixf&\s+lastSampledMatrix\s*=\s*blendBoneTransform->second;' `
    -Missing $missing

Test-Pattern -Name "Local attacks finalize serialized knockdown after native hit reaction" -Text ($mechanicsHeader + "`n" + $mechanicsHelper + "`n" + $npc + "`n" + $creature) `
    -Pattern 'finalizeLocalAttackReaction\(const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim\).*bool\s+attackTargetsPtr\(const\s+mwmp::Attack&\s+attack,\s*const\s+MWWorld::Ptr&\s+victim\).*const\s+mwmp::Target\s+victimTarget\s*=\s*MechanicsHelper::getTarget\(victim\);.*attack\.target\.guid\s*==\s*victimTarget\.guid.*attack\.target\.refId\s*==\s*victimTarget\.refId.*void\s+MechanicsHelper::finalizeLocalAttackReaction\(const\s+MWWorld::Ptr&\s+attacker,\s*const\s+MWWorld::Ptr&\s+victim\).*attack\s*==\s*nullptr\s*\|\|\s*!attack->shouldSend\s*\|\|\s*!attack->success\s*\|\|\s*attack->block\s*\|\|\s*!attackTargetsPtr\(\*attack,\s*victim\).*attack->knockdown\s*=\s*victim\.getClass\(\)\.getCreatureStats\(victim\)\.getKnockedDown\(\);.*attack->waitingForHitReaction\s*=\s*false;.*attack->hitReactionWaitFrames\s*=\s*0;.*Npc::onHit.*setHitRecovery\(true\);.*MechanicsHelper::finalizeLocalAttackReaction\(attacker,\s*ptr\);.*Creature::onHit.*setHitRecovery\(true\);.*MechanicsHelper::finalizeLocalAttackReaction\(attacker,\s*ptr\);' `
    -Missing $missing

Test-Pattern -Name "Local attack packets wait for Lua/native hit reaction before send" -Text ($baseStructs + "`n" + $mechanicsHeader + "`n" + $mechanicsHelper + "`n" + $localPlayer + "`n" + $localActor) `
    -Pattern 'bool\s+waitingForHitReaction\s*=\s*false;.*unsigned\s+int\s+hitReactionWaitFrames\s*=\s*0;.*unsigned\s+int\s+getLocalHitReactionWaitFrames\(\).*return\s+4;.*attack->waitingForHitReaction\s*=\s*attack->isHit\s*&&\s*attack->success\s*&&\s*!attack->block;.*attack->hitReactionWaitFrames\s*=\s*attack->waitingForHitReaction\s*\?\s*getLocalHitReactionWaitFrames\(\)\s*:\s*0;.*attack->waitingForHitReaction\s*=\s*attack->isHit\s*&&\s*attack->success;.*attack->hitReactionWaitFrames\s*=\s*attack->waitingForHitReaction\s*\?\s*getLocalHitReactionWaitFrames\(\)\s*:\s*0;.*bool\s+MechanicsHelper::shouldDeferLocalAttack\(Attack&\s+attack\).*if\s*\(!attack\.shouldSend\s*\|\|\s*!attack\.waitingForHitReaction\).*if\s*\(attack\.hitReactionWaitFrames\s*>\s*0\).*--attack\.hitReactionWaitFrames;.*attack\.waitingForHitReaction\s*=\s*false;.*const\s+bool\s+attackReady\s*=\s*attack\.shouldSend\s*&&\s*!MechanicsHelper::shouldDeferLocalAttack\(attack\);.*if\s*\(attackReady\s*\|\|\s*cast\.shouldSend\).*if\s*\(attackReady\).*ID_PLAYER_ATTACK.*const\s+bool\s+attackReady\s*=\s*attack\.shouldSend\s*&&\s*!MechanicsHelper::shouldDeferLocalAttack\(attack\);.*if\s*\(attackReady\s*\|\|\s*cast\.shouldSend\).*if\s*\(attackReady\).*addAttackActor\(\*this\);' `
    -Missing $missing

Test-AbsentPattern -Name "Remote attack replay does not reroll melee block" -Text $mechanicsHelper `
    -Pattern 'blockMeleeAttack\(' `
    -Missing $missing

Test-Pattern -Name "Accepted hit replay only publishes local authoritative dynamic stats" -Text ($mechanicsHelper + "`n" + $dedicatedPlayer + "`n" + $dedicatedActor + "`n" + $localActor + "`n" + $clientCell) `
    -Pattern '^(?!.*cacheDynamicStatsFromPtr\(dedicatedPlayer->creatureStats,\s*ptr\))(?!.*cacheDynamicStatsFromPtr\(dedicatedActor->creatureStats,\s*ptr\))(?=.*void\s+updateDynamicStatsFromPtr\(const\s+MWWorld::Ptr&\s+ptr,\s*bool\s+sendLocalActorsImmediately\).*getLocalPlayer\(\)->updateStatsDynamic\(true\).*dedicatedPlayer->restoreDynamicStats\(\);.*if\s*\(sendLocalActorsImmediately\)\s*localActor->sendStatsDynamic\(\);.*else\s+localActor->updateStatsDynamic\(true\);.*dedicatedActor->restoreDynamicStats\(\);)(?=.*void\s+syncDynamicStatsFromPtr\(const\s+MWWorld::Ptr&\s+ptr\).*updateDynamicStatsFromPtr\(ptr,\s*true\);)(?=.*void\s+DedicatedPlayer::restoreDynamicStats\(\).*for\s*\(int\s+i\s*=\s*0;\s*i\s*<\s*3;\s*\+\+i\).*ptrCreatureStats->setDynamic\(i,\s*value\);)(?=.*void\s+DedicatedActor::restoreDynamicStats\(\).*setStatsDynamic\(\);)(?=.*if\s*\(attack\.success\)\s*syncDynamicStatsFromPtr\(victim\);)(?=.*void\s+LocalActor::sendStatsDynamic\(\).*ID_ACTOR_STATS_DYNAMIC)(?=.*void\s+Cell::readStatsDynamic\(ActorList&\s+actorList\).*actor->creatureStats\s*=\s*baseActor\.creatureStats;.*actor->hasStatsDynamicData\s*=\s*true;.*actor->setStatsDynamic\(\);).*' `
    -Missing $missing

Test-Pattern -Name "Network hit replay falls back to authoritative damage when vanilla side effects throw" -Text $mechanicsHelper `
    -Pattern 'void\s+applyNetworkHitDamageFallback\(\s*const\s+MWWorld::Ptr&\s+victim,\s*bool\s+isHealthDamage,\s*float\s+damage,\s*float\s+healthBefore,\s*float\s+fatigueBefore\).*damage\s*<\s*0\.001f.*if\s*\(isHealthDamage\).*const\s+float\s+expectedHealth\s*=\s*healthBefore\s*-\s*damage;.*health\.setCurrent\(expectedHealth\);.*else.*const\s+float\s+expectedFatigue\s*=\s*fatigueBefore\s*-\s*damage;.*fatigue\.setCurrent\(expectedFatigue,\s*true\);.*try\s*\{.*victim\.getClass\(\)\.onHit\(victim,\s*damages,\s*object,\s*attacker,\s*attack\.success,.*catch\s*\(const\s+std::exception&\s+e\).*Network hit side effects failed.*if\s*\(attack\.success\)\s*applyNetworkHitDamageFallback\(victim,\s*isHealthDamage,\s*damage,\s*healthBefore,\s*fatigueBefore\);.*applyAttackReaction\(attack,\s*victim,\s*damage\);.*if\s*\(attack\.success\)\s*syncDynamicStatsFromPtr\(victim\);' `
    -Missing $missing

Test-Pattern -Name "Local outgoing combat syncs authoritative dynamic stats without losing player-caused actor damage" -Text ($mechanicsHeader + "`n" + $mechanicsHelper + "`n" + $localPlayer + "`n" + $localActor + "`n" + $actorList) `
    -Pattern 'publishLocalDynamicStatsFromPtr\(const\s+MWWorld::Ptr&\s+ptr,\s*bool\s+sendLocalActorsImmediately\).*ptr\s*==\s*getCurrentPlayerPtr\(\).*getLocalPlayer\(\)->updateStatsDynamic\(true\).*cellController->isLocalActor\(ptr\).*if\s*\(sendLocalActorsImmediately\)\s*localActor->sendStatsDynamic\(\);.*else\s+localActor->updateStatsDynamic\(true\);.*syncLocalDynamicStatsForTarget\(const\s+mwmp::Target&\s+target\).*publishLocalDynamicStatsFromPtr\(resolveTargetPtr\(target\),\s*true\);.*queueLocalDynamicStatsForTarget\(const\s+mwmp::Target&\s+target\).*publishLocalDynamicStatsFromPtr\(resolveTargetPtr\(target\),\s*false\);.*void\s+LocalPlayer::updateAttackOrCast\(\).*if\s*\(attackReady\).*ID_PLAYER_ATTACK.*if\s*\(attack\.isHit\s*&&\s*attack\.success\)\s*MechanicsHelper::syncLocalDynamicStatsForTarget\(attack\.target\);.*if\s*\(castReleased\).*updateStatsDynamic\(true\);.*if\s*\(castSucceeded\)\s*MechanicsHelper::syncLocalDynamicStatsForTarget\(cast\.target\);.*void\s+LocalActor::updateAttackOrCast\(\).*if\s*\(attackReady\).*addAttackActor\(\*this\);.*if\s*\(attack\.isHit\s*&&\s*attack\.success\)\s*MechanicsHelper::queueLocalDynamicStatsForTarget\(attack\.target\);.*if\s*\(castReleased\).*updateStatsDynamic\(true\);.*if\s*\(castSucceeded\)\s*MechanicsHelper::queueLocalDynamicStatsForTarget\(cast\.target\);.*void\s+ActorList::addStatsDynamicActor\(BaseActor\s+baseActor\).*for\s*\(BaseActor&\s+actor\s*:\s*statsDynamicActors\).*if\s*\(hasSameActorIdentity\(actor,\s*baseActor\)\).*actor\s*=\s*baseActor;.*return;.*statsDynamicActors\.push_back\(baseActor\);' `
    -Missing $missing

Test-Pattern -Name "Actor dynamic stat packets reject non-finite snapshots" -Text ($actorStatsAuthority + "`n" + $actorStatsDynamicPacket + "`n" + $basePacketTest) `
    -Pattern 'isFiniteDynamicStat\(const\s+ESM::StatState<float>&\s+stat\).*std::isfinite\(stat\.mBase\).*std::isfinite\(stat\.mCurrent\).*std::isfinite\(stat\.mMod\).*hasFiniteActorDynamicStats\(const\s+BaseActor&\s+actor\).*PacketActorStatsDynamic::Actor.*!hasFiniteActorDynamicStats\(actor\).*packetValid\s*=\s*false.*actorStatsDynamicRejectsNonFiniteValues' `
    -Missing $missing

Test-Pattern -Name "Player dynamic stat packets reject non-finite health increases and client revives" -Text ($basePlayer + "`n" + $playerStatsDynamicPacket + "`n" + $serverPlayerStatsDynamicProcessor + "`n" + $basePacketTest) `
    -Pattern 'bool\s+hasFiniteDynamicStats\(\)\s+const.*std::isfinite\(creatureStats\.mDynamic\[i\]\.mBase\).*std::isfinite\(creatureStats\.mDynamic\[i\]\.mCurrent\).*std::isfinite\(creatureStats\.mDynamic\[i\]\.mMod\).*void\s+restoreAcceptedStatsDynamicPacket\(\).*statsDynamicSequence\s*=\s*acceptedStatsDynamicSequence.*creatureStats\.mDynamic\[i\]\s*=\s*acceptedStatsDynamic\[i\].*statsDynamicIndexChanges\.clear\(\).*bool\s+acceptStatsDynamicPacket\(bool\s+enforceClientAuthority\s*=\s*false\).*!hasFiniteDynamicStats\(\).*restoreAcceptedStatsDynamicPacket\(\).*enforceClientAuthority\s*&&\s*hasAcceptedStatsDynamicPacket.*acceptedStatsDynamicDead\s*&&\s*!creatureStats\.mDead.*restoreAcceptedStatsDynamicPacket\(\).*incomingHealth\s*>\s*acceptedHealth\s*\+\s*healthChangeEpsilon.*restoreAcceptedStatsDynamicPacket\(\).*PacketPlayerStatsDynamic::Packet.*!player->hasFiniteDynamicStats\(\).*player->restoreAcceptedStatsDynamicPacket\(\).*packetValid\s*=\s*false.*class\s+ProcessorPlayerStatsDynamic.*if\s*\(!player\.acceptStatsDynamicPacket\(true\)\).*return;.*playerStatsDynamicRejectsNonFiniteValuesAndRestoresAcceptedSnapshot.*playerStatsDynamicDefaultReceiverAllowsServerHealthIncreaseAndRevive.*clientPlayerStatsDynamicAuthorityRejectsHealthIncreaseAndRevive' `
    -Missing $missing

Test-Pattern -Name "Server filters actor stat snapshots before save and fanout" -Text ($actorStatsAuthority + "`n" + $actorSequenceCoalescing + "`n" + $actorStatsDynamicProcessor + "`n" + $basePacketTest) `
    -Pattern 'isClientActorStatsDynamicUpdateAllowed\(const\s+BaseActor\*\s+storedActor,\s*const\s+BaseActor&\s+incoming\).*const\s+float\s+incomingHealth\s*=.*incoming\.creatureStats\.mDead\s*&&\s*incomingHealth\s*>\s*healthDeadEpsilon.*!isNewerActorStatsDynamicSequence\(incoming\.statsDynamicSequence,\s*storedActor->statsDynamicSequence\).*storedActor->creatureStats\.mDead\s*&&\s*!incoming\.creatureStats\.mDead.*incomingHealth\s*<=\s*storedHealth\s*\+\s*healthIncreaseEpsilon.*normalizeClientActorStatsDynamicUpdate\(const\s+BaseActor\*\s+storedActor,\s*BaseActor&\s+incoming\).*incoming\.creatureStats\s*=\s*storedActor->creatureStats.*incoming\.creatureStats\.mDynamic\[0\]\.mCurrent\s*=\s*std::min.*filterActorStatsDynamicToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorStatsDynamicUpdateAllowed\(currentActor,\s*actor\).*normalizeClientActorStatsDynamicUpdate\(currentActor,\s*actor\).*class\s+ProcessorActorStatsDynamic.*if\s*\(!filterActorStatsDynamicToServerAccepted\(serverCell,\s*actorList\)\)\s*return;.*serverCell->readActorList\(packetID,\s*&actorList\);.*serverCell->sendToLoaded\(&packet,\s*&actorList\);.*clientActorStatsDynamicAuthorityAllowsFirstSnapshotAndDamageOnlyHealth.*firstSnapshotDeadWithHealth.*mDynamic\[0\]\.mBase,\s*100\.f.*mDynamic\[1\]\.mCurrent,\s*20\.f.*positiveHealthDeadFlag' `
    -Missing $missing

Test-Pattern -Name "Server accepts actor death only after accepted dead stats" -Text ($actorStatsAuthority + "`n" + $actorDeathPacket + "`n" + $actorSequenceCoalescing + "`n" + $actorDeathProcessor + "`n" + $serverCell + "`n" + $basePacketTest) `
    -Pattern 'hasServerAcceptedDeadActorState\(const\s+BaseActor&\s+storedActor\).*storedActor\.hasStatsDynamicData.*hasFiniteActorDynamicStats\(storedActor\).*storedHealth\s*<=\s*healthDeadEpsilon.*isServerActorDeadForDeathPacket\(const\s+BaseActor&\s+storedActor\).*return\s+hasServerAcceptedDeadActorState\(storedActor\);.*isClientActorDeathUpdateAllowed\(const\s+BaseActor\*\s+storedActor,\s*const\s+BaseActor&\s+incoming\).*incoming\.deathState\s*==\s*0.*storedActor\s*==\s*nullptr.*!isServerActorDeadForDeathPacket\(\*storedActor\).*incoming\.statsDynamicSequence\s*==\s*storedActor->statsDynamicSequence.*RW\(actor\.statsDynamicSequence,\s*send\).*filterActorDeathToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorDeathUpdateAllowed\(currentActor,\s*actor\).*normalizeActorMovementSnapshot\(serverCell,\s*actor\);.*class\s+ProcessorActorDeath.*if\s*\(!filterActorDeathToServerAccepted\(serverCell,\s*actorList\)\)\s*return;.*serverCell->readActorList\(packetID,\s*&actorList\);.*OnActorDeath.*case\s+ID_ACTOR_DEATH:.*cellActor->creatureStats\.mDead\s*=\s*true;.*cellActor->creatureStats\.mDynamic\[0\]\.mCurrent\s*=\s*0\.f;.*cellActor->deathState\s*=\s*newActor\.deathState;.*clientActorDeathAuthorityRequiresAcceptedDeadStats.*deadFlagWithoutZeroHealth.*staleDeath\.statsDynamicSequence' `
    -Missing $missing

Test-Pattern -Name "Server rejects actor combat control from server-dead actors" -Text ($actorStatsAuthority + "`n" + $actorSequenceCoalescing + "`n" + $serverSimulation + "`n" + $actorAttackProcessor + "`n" + $actorCastProcessor + "`n" + $basePacketTest) `
    -Pattern '(?=.*isClientActorControlUpdateAllowed\(const\s+BaseActor\*\s+storedActor\).*storedActor\s*==\s*nullptr\s*\|\|\s*storedActor->creatureStats\.mDead.*storedActor->hasStatsDynamicData\s*&&\s*hasFiniteActorDynamicStats\(\*storedActor\).*storedHealth\s*<=\s*healthDeadEpsilon.*return\s+!hasServerAcceptedDeadActorState\(\*storedActor\))(?=.*filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ProcessorActorAttack.*serverCell->hasPlayer\(&player\).*acceptActorAttacks\(actorList,\s*\*serverCell\).*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);)(?=.*ProcessorActorCast.*serverCell->hasPlayer\(&player\).*acceptActorCasts\(actorList,\s*\*serverCell\).*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);)(?=.*clientActorControlAuthorityRejectsServerDeadActors)' `
    -Missing $missing

$serverAttackDamagePatternParts = @(
    'canApplyServerAttackDamage\(const\s+mwmp::Attack&\s+attack\).*attack\.isHit\s*&&\s*attack\.success\s*&&\s*!attack\.block.*std::isfinite\(attack\.damage\)',
    'getServerAttackDamage\(const\s+mwmp::Attack&\s+attack\).*std::clamp\(attack\.damage,\s*0\.f,\s*maxServerAttackDamage\)',
    'isUnarmedMeleeAttack\(const\s+mwmp::Attack&\s+attack\).*attack\.type\s*==\s*mwmp::Attack::MELEE\s*&&\s*attack\.rangedWeaponId\.empty\(\)',
    'shouldApplyUnarmedHealthDamage\(bool\s+isKnockedDown,\s*float\s+fatigue\).*isKnockedDown.*std::isfinite\(fatigue\).*fatigue\s*<=\s*0\.f',
    'shouldApplyAttackHealthDamage\(const\s+mwmp::Attack&\s+attack,\s*const\s+ESM::CreatureStats&\s+targetStats\).*isUnarmedMeleeAttack\(attack\).*shouldApplyUnarmedHealthDamage\(targetStats\.mKnockdown,\s*targetStats\.mDynamic\[2\]\.mCurrent\)',
    'shouldApplyAttackHealthDamage\(const\s+mwmp::Attack&\s+attack,\s*const\s+mwmp::SimpleCreatureStats&\s+targetStats\).*isUnarmedMeleeAttack\(attack\).*shouldApplyUnarmedHealthDamage\(false,\s*targetStats\.mDynamic\[2\]\.mCurrent\)',
    'applyHealthDamageToPlayer\(Player&\s+target,\s*float\s+damage,\s*bool&\s+becameDead\).*target\.hasFiniteDynamicStats\(\).*target\.creatureStats\.mDead.*health\s*=\s*std::max\(0\.f,\s*health\s*-\s*damage\);.*target\.creatureStats\.mDead\s*=\s*health\s*<=\s*healthDeadEpsilon;.*becameDead\s*=\s*target\.creatureStats\.mDead;.*\+\+target\.statsDynamicSequence;.*target\.acceptCurrentStatsDynamicPacket\(\);',
    'applyFatigueDamageToPlayer\(Player&\s+target,\s*float\s+damage\).*target\.hasFiniteDynamicStats\(\).*float&\s+fatigue\s*=\s*target\.creatureStats\.mDynamic\[2\]\.mCurrent;.*fatigue\s*-=\s*damage;.*target\.creatureStats\.mKnockdown\s*=.*fatigue\s*<=\s*0\.f;.*target\.statsDynamicIndexChanges\.push_back\(2\);.*target\.acceptCurrentStatsDynamicPacket\(\);',
    'applyAttackDamageToPlayer\(Player&\s+target,\s*const\s+mwmp::Attack&\s+attack,\s*bool&\s+becameDead\).*shouldApplyAttackHealthDamage\(attack,\s*target\.creatureStats\).*applyHealthDamageToPlayer\(target,\s*damage,\s*becameDead\).*applyFatigueDamageToPlayer\(target,\s*damage\)',
    'broadcastPlayerStats\(Player&\s+target\).*GetPacket\(\s*ID_PLAYER_STATS_DYNAMIC\).*statsPacket->Send\(target\.guid\);.*target\.sendToLoaded\(statsPacket\);',
    'applyHealthDamageToActor\(mwmp::BaseActor&\s+target,\s*float\s+damage\).*target\.hasStatsDynamicData.*hasFiniteActorDynamicStats\(target\).*health\s*=\s*std::max\(0\.f,\s*health\s*-\s*damage\);.*\+\+target\.statsDynamicSequence;',
    'applyFatigueDamageToActor\(mwmp::BaseActor&\s+target,\s*float\s+damage\).*target\.hasStatsDynamicData.*hasFiniteActorDynamicStats\(target\).*float&\s+fatigue\s*=\s*target\.creatureStats\.mDynamic\[2\]\.mCurrent;.*fatigue\s*-=\s*damage;.*\+\+target\.statsDynamicSequence;',
    'applyAttackDamageToActor\(mwmp::BaseActor&\s+target,\s*const\s+mwmp::Attack&\s+attack\).*shouldApplyAttackHealthDamage\(attack,\s*target\.creatureStats\).*applyHealthDamageToActor\(target,\s*damage\).*applyFatigueDamageToActor\(target,\s*damage\)',
    'broadcastActorStats\(Cell&\s+cell,\s*const\s+mwmp::BaseActor&\s+target\).*GetPacket\(\s*ID_ACTOR_STATS_DYNAMIC\).*cell\.sendToLoaded\(statsPacket,\s*&statsList\);',
    'notifyPlayerDeath\(Player&\s+target\).*Script::Call<Script::CallbackIdentity\("OnPlayerDeath"\)>\(target\.getId\(\)\);',
    'notifyPlayerStatsDynamic\(Player&\s+target\).*Script::Call<Script::CallbackIdentity\("OnPlayerStatsDynamic"\)>\(target\.getId\(\)\);',
    'notifyActorStatsDynamic\(Player&\s+source,\s*Cell&\s+cell\).*Script::Call<Script::CallbackIdentity\("OnActorStatsDynamic"\)>.*source\.getId\(\),\s*cell\.getCellData\(\)\.getDescription\(\)\.c_str\(\)',
    'ServerSimulation::applyPlayerAttack\(Player&\s+attacker\).*applyAttackDamageToPlayer\(\*target,\s*attack,\s*becameDead\).*broadcastPlayerStats\(\*target\);.*notifyPlayerStatsDynamic\(\*target\);.*if\s*\(becameDead\)\s*notifyPlayerDeath\(\*target\).*findLoadedActorTarget\(attacker,\s*attack\.target\).*applyAttackDamageToActor\(\*targetActor,\s*attack\).*notifyActorStatsDynamic\(attacker,\s*\*targetCell\)',
    'actorHasServerCombatTarget\(const\s+mwmp::BaseActor&\s+storedActor\).*storedActor\.hasAiData.*storedActor\.hasAiTarget.*storedActor\.aiAction\s*==\s*mwmp::BaseActorList::COMBAT',
    'actorCombatTargetMatchesServerAi\(Cell&\s+cell,\s*const\s+mwmp::BaseActor&\s+storedActor,\s*const\s+mwmp::Target&\s+observedTarget,\s*bool\s+allowMissingTarget\).*actorHasServerCombatTarget\(storedActor\).*allowMissingTarget\s*&&\s*isMissingActorTarget\(observedTarget\).*targetsReferToSameEntity\(storedActor\.aiTarget,\s*observedTarget\).*isAcceptedActorCombatTarget\(cell,\s*observedTarget\)',
    'isAcceptedActorAttackObservation\(Cell&\s+cell,\s*const\s+mwmp::BaseActor&\s+storedActor,\s*const\s+mwmp::BaseActor&\s+observedActor\).*canApplyServerAttackDamage\(observedActor\.attack\).*actorCombatTargetMatchesServerAi\(cell,\s*storedActor,\s*observedActor\.attack\.target,\s*!isDamageEvent\).*if\s*\(isDamageEvent\)\s*return\s+false;.*isMissingActorTarget\(observedActor\.attack\.target\).*isAcceptedActorCombatTarget\(cell,\s*observedActor\.attack\.target\)',
    'ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*Player\*\s+source\s*=\s*Players::getPlayer\(actorList\.guid\);.*isActorCombatSequenceAllowed\(\*currentActor,\s*actor\).*normalizeActorMovementSnapshot\(&serverCell,\s*actor\).*isAcceptedActorAttackObservation\(serverCell,\s*\*currentActor,\s*actor\).*acceptActorCombatSequence\(\*currentActor,\s*actor\).*acceptedActors\.push_back\(actor\).*applyAttackDamageToPlayer\(\*target,\s*attack,\s*becameDead\).*broadcastPlayerStats\(\*target\);.*notifyPlayerStatsDynamic\(\*target\);.*if\s*\(becameDead\)\s*notifyPlayerDeath\(\*target\).*applyAttackDamageToActor\(\*target,\s*attack\).*source\s*!=\s*nullptr.*notifyActorStatsDynamic\(\*source,\s*serverCell\).*actorList\.baseActors\s*=\s*std::move\(acceptedActors\)',
    'ProcessorPlayerAttack.*acceptSequencedPlayerCombatEvent\(player\).*getServerSimulation\(\)\.applyPlayerAttack\(player\);',
    'ProcessorActorAttack.*serverCell->hasPlayer\(&player\).*getServerSimulation\(\)\.acceptActorAttacks\(actorList,\s*\*serverCell\).*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);'
)
$serverAttackDamagePattern = ($serverAttackDamagePatternParts | ForEach-Object { "(?=.*$_)" }) -join ""
Test-Pattern -Name "Server applies accepted attack damage to authoritative dynamic stats" -Text ($serverSimulation + "`n" + $serverPlayerAttackProcessor + "`n" + $actorAttackProcessor) `
    -Pattern $serverAttackDamagePattern `
    -Missing $missing

$serverCastAuthorityPatternParts = @(
    'hasValidCastShape\(const\s+mwmp::Cast&\s+cast\).*cast\.type\s*!=\s*mwmp::Cast::REGULAR.*cast\.type\s*!=\s*mwmp::Cast::ITEM.*cast\.type\s*==\s*mwmp::Cast::REGULAR.*!cast\.spellId\.empty\(\).*!cast\.itemId\.empty\(\)',
    'hasExplicitActorTarget\(const\s+mwmp::Target&\s+target\).*target\.refNum\s*!=\s*static_cast<unsigned\s+int>\(-1\).*target\.mpNum\s*!=\s*static_cast<unsigned\s+int>\(-1\)',
    'castHasReleasedOutcome\(const\s+mwmp::Cast&\s+cast\).*if\s*\(cast\.pressed\).*return\s+false;.*return\s+cast\.success\s*\|\|\s*cast\.type\s*==\s*mwmp::Cast::ITEM;',
    'isAcceptedPlayerCastTarget\(const\s+mwmp::Target&\s+target,\s*const\s+ESM::Cell&\s+casterCell\).*Players::getPlayer\(target\.guid\).*targetPlayer\s*==\s*nullptr.*isLivePlayerAiTarget\(\*targetPlayer\).*isSameSimulationCell\(targetPlayer->cell,\s*casterCell\)',
    'isAcceptedCastTarget\(const\s+mwmp::Cast&\s+cast,\s*const\s+ESM::Cell&\s+casterCell,\s*Player\*\s+playerCaster,\s*Cell\*\s+serverCell\).*if\s*\(!hasValidCastShape\(cast\)\).*return\s+false;.*if\s*\(!castHasReleasedOutcome\(cast\)\).*return\s+true;.*if\s*\(cast\.target\.isPlayer\).*isAcceptedPlayerCastTarget\(cast\.target,\s*casterCell\).*if\s*\(!hasExplicitActorTarget\(cast\.target\)\).*return\s+true;.*serverCell\s*!=\s*nullptr.*findActorTarget\(\*serverCell,\s*cast\.target\).*playerCaster\s*!=\s*nullptr.*findLoadedActorTarget\(\*playerCaster,\s*cast\.target\)',
    'isAcceptedActorCastObservation\(Cell&\s+cell,\s*const\s+mwmp::BaseActor&\s+storedActor,\s*const\s+mwmp::BaseActor&\s+observedActor\).*hasValidCastShape\(observedActor\.cast\).*castHasReleasedOutcome\(observedActor\.cast\).*actorCombatTargetMatchesServerAi\(cell,\s*storedActor,\s*observedActor\.cast\.target,\s*!hasTargetedOutcome\).*isAcceptedCastTarget\(observedActor\.cast,\s*cell\.getCellData\(\),\s*nullptr,\s*&cell\)',
    'ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*for\s*\(BaseActor\s+actor\s*:\s*actorList\.baseActors\).*isActorCombatSequenceAllowed\(\*currentActor,\s*actor\).*normalizeActorMovementSnapshot\(&serverCell,\s*actor\).*isAcceptedActorCastObservation\(serverCell,\s*\*currentActor,\s*actor\).*acceptActorCombatSequence\(\*currentActor,\s*actor\).*acceptedActors\.push_back\(actor\).*actorList\.count\s*=\s*static_cast<unsigned\s+int>\(actorList\.baseActors\.size\(\)\);',
    'ServerSimulation::acceptPlayerCast\(Player&\s+caster\).*isAcceptedCastTarget\(caster\.cast,\s*caster\.cell,\s*&caster,\s*nullptr\)',
    'ProcessorPlayerCast.*acceptSequencedPlayerCombatEvent\(player\).*getServerSimulation\(\)\.acceptPlayerCast\(player\)',
    'ProcessorActorCast.*serverCell->hasPlayer\(&player\).*getServerSimulation\(\)\.acceptActorCasts\(actorList,\s*\*serverCell\)'
)
$serverCastAuthorityPattern = ($serverCastAuthorityPatternParts | ForEach-Object { "(?=.*$_)" }) -join ""
Test-Pattern -Name "Server accepts casts only through simulation-owned target validation" -Text ($serverSimulation + "`n" + $serverPlayerCastProcessor + "`n" + $actorCastProcessor) `
    -Pattern $serverCastAuthorityPattern `
    -Missing $missing

Test-Pattern -Name "Dynamic stat packets reject stale combat health snapshots by sequence" -Text ($basePlayer + "`n" + $baseActor + "`n" + $playerStatsDynamicPacket + "`n" + $actorStatsDynamicPacket + "`n" + $serverPlayerStatsDynamicProcessor + "`n" + $clientPlayerStatsDynamicProcessor + "`n" + $localPlayer + "`n" + $localActor + "`n" + $serverCell + "`n" + $clientCell + "`n" + $statsFunctions + "`n" + $actorsFunctions + "`n" + $basePacketTest) `
    -Pattern '(?=.*isNewerPlayerStatsDynamicSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\))(?=.*void\s+acceptCurrentStatsDynamicPacket\(\).*acceptedStatsDynamicSequence\s*=\s*statsDynamicSequence.*acceptedStatsDynamic\[i\]\s*=\s*creatureStats\.mDynamic\[i\])(?=.*void\s+restoreAcceptedStatsDynamicPacket\(\).*statsDynamicSequence\s*=\s*acceptedStatsDynamicSequence.*creatureStats\.mDynamic\[i\]\s*=\s*acceptedStatsDynamic\[i\])(?=.*bool\s+acceptStatsDynamicPacket\(bool\s+enforceClientAuthority\s*=\s*false\).*restoreAcceptedStatsDynamicPacket\(\).*return\s+false;)(?=.*RW\(player->statsDynamicSequence,\s*send\))(?=.*class\s+ProcessorPlayerStatsDynamic.*if\s*\(!player\.acceptStatsDynamicPacket\(true\)\).*return;)(?=.*else\s+if\s*\(player\s*!=\s*0\s*&&\s*player->acceptStatsDynamicPacket\(\)\))(?=.*void\s+LocalPlayer::updateStatsDynamic\(bool\s+forceUpdate\).*exchangeFullInfo\s*=\s*false;.*\+\+statsDynamicSequence;.*acceptCurrentStatsDynamicPacket\(\);)(?=.*isNewerActorStatsDynamicSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\))(?=.*std::uint32_t\s+statsDynamicSequence\s*=\s*0;)(?=.*RW\(actor\.statsDynamicSequence,\s*send\))(?=.*case\s+ID_ACTOR_STATS_DYNAMIC:.*!mwmp::isNewerActorStatsDynamicSequence\(newActor\.statsDynamicSequence,\s*cellActor->statsDynamicSequence\).*cellActor->statsDynamicSequence\s*=\s*newActor\.statsDynamicSequence)(?=.*void\s+Cell::readStatsDynamic\(ActorList&\s+actorList\).*!isNewerActorStatsDynamicSequence\(baseActor\.statsDynamicSequence,\s*actor->statsDynamicSequence\).*actor->statsDynamicSequence\s*=\s*baseActor\.statsDynamicSequence)(?=.*bool\s+LocalActor::storeStatsDynamic\(bool\s+forceUpdate\).*\+\+statsDynamicSequence;)(?=.*sendStatsDynamicActors\(\);.*sendDeathActors\(\);)(?=.*void\s+StatsFunctions::SendStatsDynamic\(unsigned\s+short\s+pid\).*\+\+player->statsDynamicSequence;.*player->acceptCurrentStatsDynamicPacket\(\);)(?=.*advanceActorStatsDynamicSequences\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*actor\.statsDynamicSequence\s*=\s*storedActor->statsDynamicSequence\s*\+\s*1;)(?=.*playerStatsDynamicRejectsStaleSequencesAndRestoresAcceptedSnapshot)' `
    -Missing $missing

Test-Pattern -Name "Compact player dynamic stat packets replay only changed indexes" -Text ($localPlayer + "`n" + $dedicatedPlayer + "`n" + $basePacketTest) `
    -Pattern '(?=.*void\s+LocalPlayer::setDynamicStats\(\).*auto\s+applyDynamicStat\s*=\s*\[&\].*if\s*\(exchangeFullInfo\).*for\s*\(int\s+i\s*=\s*0;\s*i\s*<\s*3;\s*\+\+i\).*applyDynamicStat\(i\);.*else.*for\s*\(auto\s+statsDynamicIndex\s*:\s*statsDynamicIndexChanges\).*applyDynamicStat\(statsDynamicIndex\);)(?=.*void\s+DedicatedPlayer::setStatsDynamic\(\).*auto\s+applyDynamicStat\s*=\s*\[&\].*if\s*\(exchangeFullInfo\).*for\s*\(int\s+i\s*=\s*0;\s*i\s*<\s*3;\s*\+\+i\).*applyDynamicStat\(i\);.*else.*for\s*\(auto\s+statsDynamicIndex\s*:\s*statsDynamicIndexChanges\).*applyDynamicStat\(statsDynamicIndex\);)(?=.*playerStatsDynamicCompactRoundTripPreservesOmittedDynamicState)' `
    -Missing $missing

Test-Pattern -Name "Local spell casts serialize start and deterministic release results" -Text ($mechanicsHeader + "`n" + $character + "`n" + $worldImp) `
    -Pattern 'queueLocalCastStart\(const\s+MWWorld::Ptr&\s+caster,\s*const\s+ESM::RefId&\s+spellId\).*queueLocalCastRelease\(const\s+MWWorld::Ptr&\s+caster,\s*const\s+MWWorld::Ptr&\s+target,\s*char\s+castType,\s*const\s+ESM::RefId&\s+id,\s*bool\s+success,\s*bool\s+instant\).*queueLocalCastStart\(mPtr,\s*spellid\).*queueLocalCastRelease\(.*Cast::REGULAR.*selectedSpell,\s*success,\s*scriptedSpell\).*queueLocalCastRelease\(.*Cast::ITEM.*itemPtr\.getCellRef\(\)\.getRefId\(\),\s*success,\s*scriptedSpell\)' `
    -Missing $missing

Test-Pattern -Name "Remote spell replay validates packet data and gates effect mutation behind authoritative mode" -Text $mechanicsHelper `
    -Pattern 'void\s+MechanicsHelper::processCast\(Cast\s+cast,\s*const\s+MWWorld::Ptr&\s+caster,\s*bool\s+applyAuthoritativeState\).*Ignoring cast with invalid type.*Ignoring cast with empty spell or item id.*Ignoring cast with invalid projectile origin.*MWWorld::Ptr\s+victim;.*bool\s+castApplied\s*=\s*false;.*if\s*\(!applyAuthoritativeState\)\s*return;.*if\s*\(cast\.success\).*victim\s*=\s*resolveTargetPtr\(cast\.target,\s*caster\.getCell\(\)\);.*MWMechanics::CastSpell remoteCast\(caster,\s*victim,\s*false,\s*cast\.instant\);.*remoteCast\.mHitPosition\s*=\s*getSpellHitPosition\(caster,\s*victim\);.*remoteCast\.mAlwaysSucceed\s*=\s*true;.*castApplied\s*=\s*remoteCast\.cast\(spell\);.*if\s*\(!applyAuthoritativeState\)\s*return;.*victim\s*=\s*resolveTargetPtr\(cast\.target,\s*caster\.getCell\(\)\);.*MWMechanics::CastSpell remoteCast\(caster,\s*victim,\s*false,\s*cast\.instant\);.*castApplied\s*=\s*remoteCast\.cast\(\*it\);.*if\s*\(castApplied\)\s*syncDynamicStatsAfterCombatEffect\(caster,\s*victim\);' `
    -Missing $missing

Test-AbsentPattern -Name "Remote spell replay does not call World::castSpell for local retarget/reroll" -Text $mechanicsHelper `
    -Pattern 'getWorld\(\)->castSpell\(caster\)' `
    -Missing $missing

Test-Pattern -Name "Combat side-channel packets normalize sequenced movement without dropping same-snapshot gameplay" -Text ($dedicatedPlayerHeader + "`n" + $dedicatedPlayer + "`n" + $playerAttackProcessor + "`n" + $playerCastProcessor + "`n" + $clientCell) `
    -Pattern 'bool\s+normalizePositionPacket\(\);.*bool\s+DedicatedPlayer::normalizePositionPacket\(\).*if\s*\(readPositionPacket\(\)\)\s*return\s+true;.*return\s+reference\s*!=\s*nullptr\s*&&\s*hasAcceptedPositionPacket;.*ProcessorPlayerAttack.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*MechanicsHelper::processAttack\(player->attack,\s*dedicatedPlayer\.getPtr\(\),\s*false\);.*ProcessorPlayerCast.*if\s*\(!dedicatedPlayer\.normalizePositionPacket\(\)\)\s*return;.*MechanicsHelper::processCast\(player->cast,\s*dedicatedPlayer\.getPtr\(\),\s*false\);.*bool\s+normalizeSequencedPositionForCombat\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*if\s*\(!baseActor\.hasPositionData\)\s*return\s+false;.*if\s*\(actor\.hasPositionData\s*&&\s*!isNewerPositionSequence\(baseActor\.positionSequence,\s*actor\.positionSequence\)\)\s*return\s+true;.*return\s+applySequencedPosition\(actor,\s*baseActor\);.*void\s+Cell::readAttack\(ActorList&\s+actorList\).*if\s*\(!normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\)\).*Ignoring ActorAttack.*movement snapshot was missing or invalid.*continue;.*MechanicsHelper::processAttack\(actor->attack,\s*actor->getPtr\(\),\s*false\);.*void\s+Cell::readCast\(ActorList&\s+actorList\).*if\s*\(!normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\)\).*Ignoring ActorCast.*movement snapshot was missing or invalid.*continue;.*MechanicsHelper::processCast\(actor->cast,\s*actor->getPtr\(\),\s*false\);' `
    -Missing $missing

Test-Pattern -Name "Server player combat packets also fan out directly to targeted players" -Text ($serverPlayerHeader + "`n" + $serverPlayer + "`n" + $serverPlayerAttackProcessor + "`n" + $serverPlayerCastProcessor) `
    -Pattern 'void\s+sendToLoadedAndGuid\(mwmp::PlayerPacket\s+\*myPacket,\s*mwmp::PacketGuid\s+targetGuid\);.*void\s+Player::sendToLoadedAndGuid\(mwmp::PlayerPacket\s+\*myPacket,\s*mwmp::PacketGuid\s+targetGuid\).*for\s*\(auto\s+loadedCell\s*:\s*cells\).*if\s*\(loadedCell\s*==\s*nullptr\)\s*continue;.*for\s*\(auto\s+pl\s*:\s*\*loadedCell\).*pl\s*!=\s*nullptr\s*&&\s*!pl->npc\.mName\.empty\(\).*if\s*\(targetGuid\s*!=\s*mwmp::unassignedPacketGuid\(\)\s*&&\s*targetGuid\s*!=\s*guid\).*Player\*\s+target\s*=\s*Players::getPlayer\(targetGuid\);.*if\s*\(target\s*!=\s*nullptr\s*&&\s*!target->npc\.mName\.empty\(\)\).*plList\.push_back\(target\);.*plList\.sort\(\);.*plList\.unique\(\);.*myPacket->setPlayer\(this\);.*myPacket->Send\(pl->guid\);.*class\s+ProcessorPlayerAttack.*if\s*\(player\.attack\.isHit\s*&&\s*player\.attack\.target\.isPlayer\)\s*player\.sendToLoadedAndGuid\(&packet,\s*player\.attack\.target\.guid\);.*else\s+player\.sendToLoaded\(&packet\);.*class\s+ProcessorPlayerCast.*if\s*\(player\.cast\.target\.isPlayer\)\s*player\.sendToLoadedAndGuid\(&packet,\s*player\.cast\.target\.guid\);.*else\s+player\.sendToLoaded\(&packet\);' `
    -Missing $missing

Test-Pattern -Name "Local player and actor attack/cast sends do not defer each other" -Text ($localPlayer + "`n" + $localActor) `
    -Pattern 'void\s+LocalPlayer::updateAttackOrCast\(\).*const\s+bool\s+attackReady\s*=.*if\s*\(attackReady\).*ID_PLAYER_ATTACK.*attack\.shouldSend\s*=\s*false;.*if\s*\(cast\.shouldSend\).*ID_PLAYER_CAST.*cast\.shouldSend\s*=\s*false;.*void\s+LocalActor::updateAttackOrCast\(\).*const\s+bool\s+attackReady\s*=.*if\s*\(attackReady\).*addAttackActor\(\*this\);.*attack\.shouldSend\s*=\s*false;.*if\s*\(cast\.shouldSend\).*addCastActor\(\*this\);.*cast\.shouldSend\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Local player publishes equipment and dynamic stats before combat packets" -Text $localPlayer `
    -Pattern 'void\s+LocalPlayer::update\(\).*updatePosition\(\);.*updateAnimFlags\(\);.*updateEquipment\(\);.*updateStatsDynamic\(\);.*updateAttackOrCast\(\);' `
    -Missing $missing

$playerStatsDynamicLuaPatternParts = @(
    'class\s+ProcessorPlayerStatsDynamic.*if\s*\(!player\.acceptStatsDynamicPacket\(true\)\).*return;.*player\.sendToLoaded\(&packet\);.*Script::Call<Script::CallbackIdentity\("OnPlayerStatsDynamic"\)>\(player\.getId\(\)\);',
    '"OnPlayerStatsDynamic".*Callback<unsigned\s+short>\(\)',
    'function\s+OnPlayerStatsDynamic\(pid\).*eventHandler\.OnPlayerStatsDynamic\(pid\)',
    'eventHandler\.OnPlayerStatsDynamic\s*=\s*function\(pid\).*eventHandler\.OnGenericPlayerEvent\(pid,\s*"PlayerStatsDynamic"\)',
    'eventHandler\.OnGenericPlayerEvent\s*=\s*function\(pid,\s*packetType\).*customEventHooks\.triggerValidators\("On"\s*\.\.\s*packetType,\s*\{pid,\s*playerPacket\}\).*Players\[pid\]:SaveDataByPacketType\(packetType,\s*playerPacket\).*customEventHooks\.triggerHandlers\("On"\s*\.\.\s*packetType,\s*eventStatus,\s*\{pid,\s*playerPacket\}\)',
    'function\s+BasePlayer:SaveDataByPacketType\(packetType,\s*playerPacket\).*packetType\s*==\s*"PlayerStatsDynamic".*self:SaveStatsDynamic\(playerPacket\)',
    'notifyPlayerStatsDynamic\(Player&\s+target\).*Script::Call<Script::CallbackIdentity\("OnPlayerStatsDynamic"\)>\(target\.getId\(\)\);',
    'EventHandlerOnPlayerStatsDynamicPersistsPlayerStats',
    'PlayerBaseRoutesStatsDynamicPacketToStatsSave'
)
$playerStatsDynamicLuaPattern = ($playerStatsDynamicLuaPatternParts | ForEach-Object { "(?=.*$_)" }) -join ""
Test-Pattern -Name "Player dynamic stat packets persist accepted player damage through Lua callback" -Text ($serverPlayerStatsDynamicProcessor + "`n" + $scriptFunctions + "`n" + $serverCore + "`n" + $eventHandler + "`n" + $playerBase + "`n" + $serverSimulation + "`n" + $serverLuaCompatTest) `
    -Pattern $playerStatsDynamicLuaPattern `
    -Missing $missing

Test-Pattern -Name "Actor dynamic stat packets persist accepted NPC damage through Lua callback" -Text ($actorStatsDynamicProcessor + "`n" + $scriptFunctions + "`n" + $serverCore + "`n" + $eventHandler + "`n" + $serverLuaCompatTest) `
    -Pattern 'class\s+ProcessorActorStatsDynamic.*serverCell->readActorList\(packetID,\s*&actorList\);.*Script::Call<Script::CallbackIdentity\("OnActorStatsDynamic"\)>\(player\.getId\(\),\s*actorList\.cell\.getDescription\(\)\.c_str\(\)\);.*serverCell->sendToLoaded\(&packet,\s*&actorList\);.*"OnActorStatsDynamic".*Callback<unsigned\s+short,\s*const\s+char\*>\(\).*function\s+OnActorStatsDynamic\(pid,\s*cellDescription\).*eventHandler\.OnActorStatsDynamic\(pid,\s*cellDescription\).*eventHandler\.OnActorStatsDynamic\s*=\s*function\(pid,\s*cellDescription\).*customEventHooks\.triggerValidators\("OnActorStatsDynamic",\s*\{pid,\s*cellDescription\}\).*LoadedCells\[cellDescription\]:SaveActorStatsDynamic\(\).*LoadedCells\[cellDescription\]:QuicksaveToDrive\(\).*customEventHooks\.triggerHandlers\("OnActorStatsDynamic",\s*eventStatus,\s*\{pid,\s*cellDescription\}\).*EventHandlerOnActorStatsDynamicPersistsLoadedCellStats' `
    -Missing $missing

Test-Pattern -Name "Combat-related saved player identities survive account and character name divergence" -Text ($packetReader + "`n" + $logicHandler + "`n" + $cellBase + "`n" + $playerBase) `
    -Pattern 'local\s+function\s+AddPlayerIdentity\(targetTable,\s*player\).*targetTable\.playerName\s*=\s*player\.accountName.*targetTable\.accountName\s*=\s*player\.accountName.*targetTable\.characterName\s*=\s*player\.name.*targetTable\.playerKey\s*=\s*player:GetCharacterStorageKey\(\).*logicHandler\.GetLoggedInPlayerByStorageKey\s*=\s*function\(targetKey\).*player:GetCharacterStorageKey\(\)\s*==\s*targetKey.*local\s+function\s+getLoggedInPlayerByStorageKey\(playerKey\).*type\(logicHandler\.GetLoggedInPlayerByStorageKey\)\s*==\s*"function".*function\s+BaseCell:SaveActorSpellsActive\(actors\).*playerKey\s*=\s*spellInstanceValues\.caster\.playerKey.*function\s+BaseCell:SaveActorDeath\(actors\).*playerKey\s*=\s*actor\.killer\.playerKey.*function\s+BasePlayer:SaveSpellsActive\(playerPacket\).*playerKey\s*=\s*spellInstanceValues\.caster\.playerKey' `
    -Missing $missing

Write-Host "TES3MP combat sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 38"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
