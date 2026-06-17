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

    if ($Name -eq "Server simulation ticks only active or server-interested cells") {
        $requiredPatterns = @(
            'void\s+ServerSimulation::tickActors\(float\s+deltaSeconds\).*for\s*\(Cell\*\s+cell\s*:\s*cellController->getCells\(\)\).*if\s*\(!cell->hasPlayers\(\)\s*&&\s*!cell->hasSimulationInterest\(\)\)\s*continue;.*BaseActorList\*\s+storedActorList\s*=\s*cell->getActorList\(\)',
            'bool\s+hasPlayers\(\)\s+const;.*bool\s+hasSimulationInterest\(\)\s+const;.*void\s+setSimulationInterest\(bool\s+enabled\);',
            'bool\s+Cell::hasPlayers\(\)\s+const.*return\s+!players\.empty\(\);',
            'bool\s+Cell::hasSimulationInterest\(\)\s+const.*return\s+simulationInterest;',
            'void\s+Cell::setSimulationInterest\(bool\s+enabled\).*simulationInterest\s*=\s*enabled;',
            'if\s*\(!c->hasPlayers\(\)\s*&&\s*!c->hasSimulationInterest\(\)\)\s*toDelete\.push_back\(c\);',
            'GetCellSimulationInterest.*CellFunctions::GetCellSimulationInterest',
            'SetCellSimulationInterest.*CellFunctions::SetCellSimulationInterest',
            'bool\s+CellFunctions::GetCellSimulationInterest\(const\s+char\s+\*cellDescription\).*CellController::get\(\)->getCell\(&esmCell\).*return\s+cell\s*!=\s*nullptr\s*&&\s*cell->hasSimulationInterest\(\);',
            'void\s+CellFunctions::SetCellSimulationInterest\(const\s+char\s+\*cellDescription,\s*bool\s+enabled\).*enabled\s*\?\s*CellController::get\(\)->addCell\(esmCell\)\s*:\s*CellController::get\(\)->getCell\(&esmCell\).*cell->setSimulationInterest\(enabled\);',
            'GetCellSimulationInterest',
            'SetCellSimulationInterest'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server-owned player cell changes carry AI followers across the accepted boundary") {
        $requiredPatterns = @(
            'mPlayerAcceptedCells\[player\.guid\]\s*=\s*player\.cell',
            'void\s+ServerSimulation::removePlayer\(PacketGuid\s+guid\).*mPlayerAcceptedCells\.erase\(guid\);',
            'bool\s+isPlayerFollowerPackage\(const\s+mwmp::BaseActor&\s+actor,\s*mwmp::PacketGuid\s+playerGuid\).*actor\.hasAiData.*actor\.hasAiTarget.*actor\.aiTarget\.isPlayer.*actor\.aiTarget\.guid\s*==\s*playerGuid.*mwmp::BaseActorList::FOLLOW.*mwmp::BaseActorList::ESCORT',
            'ESM::Position\s+makeFollowerCellChangePosition\(const\s+Player&\s+player,\s*std::size_t\s+followerIndex\).*forwardX\s*=\s*sinYaw.*rightX\s*=\s*cosYaw.*followerCellChangeBehindDistance.*followerCellChangeColumnSpacing.*position\.pos\[0\]\s*\+=\s*rightX\s*\*\s*lateral\s*-\s*forwardX\s*\*\s*behind.*position\.pos\[1\]\s*\+=\s*rightY\s*\*\s*lateral\s*-\s*forwardY\s*\*\s*behind',
            'class\s+ScopedReceivedActorList.*getReceivedActorList\(\).*mPreviousActorList\(\*mReceivedActorList\).*~ScopedReceivedActorList\(\).*mPreviousActorList',
            'persistServerGeneratedActorCellChange\(Player&\s+player,\s*mwmp::BaseActorList&\s+actorList\).*ScopedReceivedActorList\s+receivedActorList\(actorList\).*Script::Call<Script::CallbackIdentity\("OnActorCellChange"\)>',
            'moveFollowingActorsAcrossPlayerCellChange\(Player&\s+player,\s*const\s+ESM::Cell&\s+sourceCellData\).*isSameSimulationCell\(sourceCellData,\s*player\.cell\).*sourceCell->getActorList\(\)',
            'moveFollowingActorsAcrossPlayerCellChange\(Player&\s+player,\s*const\s+ESM::Cell&\s+sourceCellData\).*movedFollowers\.guid\s*=\s*player\.guid.*isClientActorControlUpdateAllowed\(&actor\).*isPlayerFollowerPackage\(actor,\s*player\.guid\).*movedActor\.cell\s*=\s*player\.cell',
            'moveFollowingActorsAcrossPlayerCellChange\(Player&\s+player,\s*const\s+ESM::Cell&\s+sourceCellData\).*movedActor\.position\s*=\s*makeFollowerCellChangePosition\(player,\s*followerIndex\).*movedActor\.isFollowerCellChange\s*=\s*true.*movedActor\.hasPositionData\s*=\s*true',
            'moveFollowingActorsAcrossPlayerCellChange\(Player&\s+player,\s*const\s+ESM::Cell&\s+sourceCellData\).*persistServerGeneratedActorCellChange\(player,\s*movedFollowers\).*ActorProcessor::cacheCellChange\(movedFollowers\).*ActorProcessor::sendCellChangeToLoaded\(\*actorPacket,\s*movedFollowers\).*actorPacket->Send\(player\.guid\)',
            'ServerSimulation::acceptPlayerCellChange\(Player&\s+player,\s*PlayerPacket&\s+packet\).*previousCellIt\s*=\s*mPlayerAcceptedCells\.find\(player\.guid\).*previousAcceptedCell\s*=\s*previousCellIt->second',
            'ServerSimulation::acceptServerAuthoredPlayerState\(Player&\s+player,\s*bool\s+cellChangePacket\).*mPlayerAcceptedCells\[player\.guid\]\s*=\s*player\.cell',
            'ServerSimulation::acceptPlayerCellChange\(Player&\s+player,\s*PlayerPacket&\s+packet\).*acceptServerAuthoredPlayerState\(player,\s*true\).*moveFollowingActorsAcrossPlayerCellChange\(player,\s*previousAcceptedCell\)'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Actor animation flag packets are sequenced and preserve newer movement sidecars") {
        $requiredPatterns = @(
            'bool\s+isNewerActorAnimFlagsSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);',
            'mergeNewestActorPosition\(BaseActor&\s+target,\s*const\s+BaseActor&\s+incoming\).*isNewerPositionSequence\(incoming\.positionSequence,\s*target\.positionSequence\)',
            'mergeNewestActorAnimFlags\(BaseActor&\s+target,\s*const\s+BaseActor&\s+incoming\).*mergeNewestActorPosition\(target,\s*incoming\).*isNewerActorAnimFlagsSequence\(incoming\.animFlagsSequence,\s*target\.animFlagsSequence\)',
            'PacketActorAnimFlags::PacketActorAnimFlags\(\).*packetID\s*=\s*ID_ACTOR_ANIM_FLAGS;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*actor\.hasAnimFlagsData\s*=\s*true;',
            'void\s+LocalActor::updateAnimFlags\(bool\s+forceUpdate\).*\+\+animFlagsSequence;.*getActorList\(\)->addAnimFlagsActor\(\*this\);',
            'void\s+Cell::readAnimFlags\(ActorList&\s+actorList\).*applySequencedPosition\(\*actor,\s*baseActor\);.*actor->setAnimFlags\(\);',
            'filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*std::vector<BaseActor>\*\s+correctionActors.*hasNewerPosition.*hasNewerAnimFlags.*acceptNewestAnimFlagsActor\(acceptedActors,\s*acceptedActorIndexes,\s*actor\);',
            'ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList,\s*&correctionActors\).*serverCell->sendToLoaded\(&packet,\s*&actorList\);',
            'case\s+ID_ACTOR_ANIM_FLAGS:.*actorToCache.*!isFiniteActorMovementSnapshot\(actorToCache\).*mergeNewestActorAnimFlags\(\*cellActor,\s*actorToCache\);',
            'actorAnimFlagsRoundTripsSequenceAndState',
            'actorAnimFlagsRoundTripsWithoutMovementSnapshot',
            'actorAnimFlagsMergeKeepsNewestMovementAndFlags'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server drops movement and AI control from server-dead actors") {
        $requiredPatterns = @(
            'hasServerAcceptedDeadActorState\(const\s+BaseActor&\s+storedActor\).*storedActor\.creatureStats\.mDead.*storedHealth\s*<=\s*healthDeadEpsilon',
            'isClientActorControlUpdateAllowed\(const\s+BaseActor\*\s+storedActor\).*hasServerAcceptedDeadActorState\(\*storedActor\)',
            'filterActorListToKnownLiveActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,.*isClientActorControlUpdateAllowed\(currentActor\)',
            'filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList',
            'ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\)',
            'ProcessorActorAttack.*serverCell->hasPlayer\(&player\).*acceptActorAttacks\(actorList,\s*\*serverCell\)',
            'ProcessorActorCast.*serverCell->hasPlayer\(&player\).*acceptActorCasts\(actorList,\s*\*serverCell\)',
            'ProcessorActorAI.*serverCell->hasPlayer\(&player\).*acceptActorAiSnapshot\(actorList,\s*\*serverCell\)',
            'ProcessorActorCellChange.*isClientActorControlUpdateAllowed\(currentActor\)',
            'clientActorControlAuthorityRejectsServerDeadActors'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server actor movement batches forward newest movement and anim flag sequences per actor") {
        $requiredPatterns = @(
            'using\s+ActorIdentityKey\s*=\s*std::pair<unsigned\s+int,\s*unsigned\s+int>;',
            'acceptNewestPositionActor\(std::vector<BaseActor>&\s+acceptedActors,\s*std::map<ActorIdentityKey,\s*std::size_t>&\s+acceptedActorIndexes,\s*const\s+BaseActor&\s+actor\).*isNewerPositionSequence\(actor\.positionSequence,\s*acceptedActor\.positionSequence\).*acceptedActor\s*=\s*actor;',
            'acceptNewestAnimFlagsActor\(std::vector<BaseActor>&\s+acceptedActors,\s*std::map<ActorIdentityKey,\s*std::size_t>&\s+acceptedActorIndexes,\s*const\s+BaseActor&\s+actor\).*mergeNewestActorAnimFlags\(acceptedActor,\s*actor\);',
            'filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*std::vector<BaseActor>\*\s+correctionActors\s*=\s*nullptr\).*std::map<ActorIdentityKey,\s*std::size_t>\s+acceptedActorIndexes;.*std::map<ActorIdentityKey,\s*std::size_t>\s+correctionActorIndexes;',
            'hasNewerPosition.*hasNewerAnimFlags.*acceptNewestAnimFlagsActor\(acceptedActors,\s*acceptedActorIndexes,\s*actor\);',
            'ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*acceptNewestPositionActor\(acceptedActors,\s*acceptedActorIndexes,\s*simulatedActor\);',
            'ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList,\s*&correctionActors\)'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server actor movement cache and broadcasts reject non-finite snapshots") {
        $requiredPatterns = @(
            'isFiniteActorPosition\(const\s+ESM::Position&\s+position\).*std::isfinite\(position\.pos\[0\]\).*std::isfinite\(position\.rot\[2\]\)',
            'isFiniteActorMovementSnapshot\(const\s+BaseActor&\s+actor\).*isFiniteActorPosition\(actor\.position\)\s*&&\s*isFiniteActorPosition\(actor\.direction\)',
            'normalizeActorMovementSnapshot\(Cell\*\s+serverCell,\s*BaseActor&\s+actor\).*actor\.hasPositionData\s*&&\s*!isFiniteActorMovementSnapshot\(actor\).*actor\.hasPositionData\s*=\s*false;',
            'filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*std::vector<BaseActor>\*\s+correctionActors.*actor\.hasPositionData\s*&&\s*!isFiniteActorMovementSnapshot\(actor\).*actor\.hasPositionData\s*=\s*false;',
            'ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*if\s*\(!isFiniteActorMovementSnapshot\(actor\)\).*addPositionCorrection\(\*currentActor\);.*continue;',
            'ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList,\s*&correctionActors\)',
            'ProcessorActorCellChange.*if\s*\(isFiniteActorMovementSnapshot\(actor\).*actor\.cell\.getDescription\(\)\s*!=\s*actorList\.cell\.getDescription\(\)\).*acceptedActors\.push_back\(actor\);',
            'case\s+ID_ACTOR_POSITION:.*if\s*\(!isFiniteActorMovementSnapshot\(newActor\)\).*break;',
            'case\s+ID_ACTOR_ANIM_FLAGS:.*actorToCache.*!isFiniteActorMovementSnapshot\(actorToCache\).*actorToCache\.hasPositionData\s*=\s*false;.*mergeNewestActorAnimFlags\(\*cellActor,\s*actorToCache\);'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server registers actors through actor-list packets and drops unknown actor updates") {
        $requiredPatterns = @(
            'filterActorListToKnownActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*serverCell->containsActor\(actor\.refNum,\s*actor\.mpNum\)',
            'filterActorListToKnownLiveActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*std::vector<BaseActor>\*\s+correctionActors.*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\)',
            'filterActorDeathToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\)',
            'case\s+ID_ACTOR_LIST:.*cellActor->refId\s*=\s*newActor\.refId;',
            'void\s+Cell::requestActorListFrom\(const\s+mwmp::PacketGuid&\s+guid\).*actorListRequestGuid\s*=\s*guid;',
            'void\s+ActorFunctions::SendActorList\(\).*writeActorList\.action\s*==\s*mwmp::BaseActorList::REQUEST.*serverCell->requestActorListFrom\(writeActorList\.guid\);',
            'ProcessorActorList.*serverCell->readActorList\(packetID,\s*&actorList\);',
            'ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*BaseActor\*\s+currentActor\s*=\s*serverCell\.getActor\(actor\.refNum,\s*actor\.mpNum\);.*if\s*\(currentActor\s*==\s*nullptr\)\s*continue;',
            'ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList,\s*&correctionActors\)',
            'ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\)',
            'ProcessorActorAttack.*acceptActorAttacks\(actorList,\s*\*serverCell\)',
            'ProcessorActorCast.*acceptActorCasts\(actorList,\s*\*serverCell\)',
            'ProcessorActorDeath.*filterActorDeathToServerAccepted\(serverCell,\s*actorList\)',
            'ProcessorActorAI.*serverCell->hasPlayer\(&player\).*acceptActorAiSnapshot\(actorList,\s*\*serverCell\)',
            'ProcessorActorEquipment.*filterActorEquipmentToServerAccepted\(serverCell,\s*actorList\)',
            'ProcessorActorSpeech.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\)',
            'ProcessorActorSpellsActive.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\)',
            'ProcessorActorStatsDynamic.*filterActorStatsDynamicToServerAccepted\(serverCell,\s*actorList\)',
            'ProcessorActorTest.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\)'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if ($Name -eq "Server simulation derives actor movement from cached AI packages") {
        $requiredPatterns = @(
            'constexpr\s+float\s+aiCoordinateStopDistance\s*=\s*64\.f;',
            'constexpr\s+float\s+aiTargetStopDistance\s*=\s*128\.f;',
            'struct\s+ActorWanderState.*remainingDecisionSeconds.*decisionSequence.*hasOrigin.*hasDestination',
            'std::map<ActorMovementKey,\s*ActorWanderState>\s+mActorWanderStates;',
            'constexpr\s+float\s+aiWanderStopDistance\s*=\s*32\.f;.*constexpr\s+float\s+aiWanderMinimumDecisionSeconds\s*=\s*2\.f;.*constexpr\s+float\s+aiWanderMaximumDecisionSeconds\s*=\s*8\.f;',
            'std::uint32_t\s+getActorWanderHash\(const\s+std::string&\s+cellKey,\s*unsigned\s+int\s+refNum,\s*unsigned\s+int\s+mpNum,\s*std::uint32_t\s+sequence,\s*std::uint32_t\s+salt\).*mixWanderHash\(hash,\s*refNum\).*mixWanderHash\(hash,\s*mpNum\).*mixWanderHash\(hash,\s*sequence\).*mixWanderHash\(hash,\s*salt\).*float\s+getUnitWanderValue\(std::uint32_t\s+hash\)',
            'bool\s+isLivePlayerAiTarget\(const\s+Player&\s+player\).*player\.creatureStats\.mDead.*player\.hasFiniteDynamicStats\(\).*player\.creatureStats\.mDynamic\[0\]\.mCurrent\s*<=\s*healthDeadEpsilon',
            'bool\s+getAiTargetPosition\(Cell&\s+cell,\s*const\s+mwmp::Target&\s+target,\s*ESM::Position&\s+destination\).*target\.isPlayer.*Players::getPlayer\(target\.guid\).*player->hasFinitePositionPacket\(\).*isLivePlayerAiTarget\(\*player\).*getCellSimulationKey\(player->cell\)\s*!=\s*getCellSimulationKey\(cell\.getCellData\(\)\).*return\s+false;.*destination\s*=\s*player->position.*findActorTarget\(cell,\s*target\).*actor->hasPositionData.*destination\s*=\s*actor->position',
            'float\s+getAiStopDistance\(const\s+mwmp::BaseActor&\s+actor,\s*bool\s+coordinatePackage\).*coordinatePackage.*aiCoordinateStopDistance.*actor\.aiDistance\s*==\s*0.*aiTargetStopDistance.*std::clamp\(static_cast<float>\(actor\.aiDistance\),\s*aiMinimumStopDistance,\s*aiMaximumStopDistance\)',
            'ESM::Position\s+simulateMovementPosition\(const\s+ESM::Position&\s+currentPosition,\s*const\s+ESM::Position&\s+observedPosition,\s*ESM::Position&\s+observedDirection,\s*float\s+deltaSeconds\).*normalizeHorizontalIntent\(horizontalX,\s*horizontalY\).*const\s+float\s+yaw\s*=.*observedPosition\.rot\[2\].*const\s+float\s+worldX\s*=\s*horizontalX\s*\*\s*cosYaw\s*\+\s*horizontalY\s*\*\s*sinYaw.*const\s+float\s+worldY\s*=\s*-horizontalX\s*\*\s*sinYaw\s*\+\s*horizontalY\s*\*\s*cosYaw',
            'bool\s+buildAiMovementIntent\(Cell&\s+cell,\s*mwmp::BaseActor&\s+actor,\s*ESM::Position&\s+direction\).*actor\.hasAiData.*actor\.aiAction.*mwmp::BaseActorList::TRAVEL.*mwmp::BaseActorList::ESCORT.*actor\.aiCoordinates.*mwmp::BaseActorList::ACTIVATE.*mwmp::BaseActorList::COMBAT.*mwmp::BaseActorList::FOLLOW.*getAiTargetPosition\(cell,\s*actor\.aiTarget,\s*destination\).*direction\s*=\s*zeroPosition\(\).*distanceSquared\s*<=\s*stopDistance\s*\*\s*stopDistance.*actor\.position\.rot\[2\]\s*=\s*std::atan2\(deltaX,\s*deltaY\).*direction\.pos\[1\]\s*=\s*1\.f',
            'void\s+chooseWanderDestination\(const\s+std::string&\s+cellKey,\s*unsigned\s+int\s+refNum,\s*unsigned\s+int\s+mpNum,\s*mwmp::BaseActor&\s+actor,\s*mwmp::ActorWanderState&\s+wanderState\).*std::clamp\(\s*static_cast<float>\(actor\.aiDistance\),\s*0\.f,\s*aiMaximumStopDistance\).*std::sin\(angle\)\s*\*\s*distance.*std::cos\(angle\)\s*\*\s*distance.*remainingDecisionSeconds',
            'bool\s+buildWanderMovementIntent\(const\s+std::string&\s+cellKey,\s*unsigned\s+int\s+refNum,\s*unsigned\s+int\s+mpNum,\s*mwmp::BaseActor&\s+actor,\s*mwmp::ActorWanderState&\s+wanderState,\s*float\s+deltaSeconds,\s*ESM::Position&\s+direction\).*actor\.aiAction\s*!=\s*mwmp::BaseActorList::WANDER.*wanderState\.hasOrigin.*chooseWanderDestination\(cellKey,\s*refNum,\s*mpNum,\s*actor,\s*wanderState\).*direction\s*=\s*zeroPosition\(\).*distanceSquared\s*<=\s*aiWanderStopDistance\s*\*\s*aiWanderStopDistance.*actor\.position\.rot\[2\]\s*=\s*std::atan2\(deltaX,\s*deltaY\).*direction\.pos\[1\]\s*=\s*1\.f',
            'bool\s+hasServerOwnedActorMovement\(const\s+mwmp::BaseActor&\s+actor\).*actor\.hasAiData.*actor\.hasPositionData.*mwmp::BaseActorList::CANCEL.*mwmp::BaseActorList::WANDER.*mwmp::BaseActorList::TRAVEL.*mwmp::BaseActorList::ESCORT.*mwmp::BaseActorList::ACTIVATE.*mwmp::BaseActorList::COMBAT.*mwmp::BaseActorList::FOLLOW',
            'ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*hasServerOwnedActorMovement\(\*currentActor\).*addPositionCorrection\(\*currentActor\);.*continue;',
            'void\s+ServerSimulation::tickActors\(float\s+deltaSeconds\).*const\s+std::string\s+cellKey\s*=\s*getCellSimulationKey\(cell->getCellData\(\)\).*const\s+ActorMovementKey\s+actorKey\s*\{\s*cellKey,\s*actor\.refNum,\s*actor\.mpNum\s*\};.*const\s+bool\s+serverOwnsActorMovement\s*=\s*hasServerOwnedActorMovement\(actor\).*buildAiMovementIntent\(\*cell,\s*actor,\s*direction\).*hasWanderMovementIntent.*mActorWanderStates\[actorKey\].*buildWanderMovementIntent\(.*mActorWanderStates\.erase\(actorKey\).*hasServerMovementIntent.*if\s*\(!hasServerMovementIntent\).*serverOwnsActorMovement.*direction\s*=\s*zeroPosition\(\).*sanitizeFinitePosition\(direction\).*if\s*\(!hasMovementIntent\(direction\)\).*\(hasServerMovementIntent\s*\|\|\s*serverOwnsActorMovement\)\s*&&\s*hasMovementIntent\(actor\.direction\).*actor\.direction\s*=\s*direction.*\+\+actor\.positionSequence.*simulateMovementPosition\(actor\.position,\s*actor\.position,\s*direction,\s*deltaSeconds\)',
            'ServerSimulation::acceptActorAiSnapshot\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*serverCell\.readActorList\(ID_ACTOR_AI,\s*&actorList\);',
            'void\s+ActorFunctions::SendActorAI\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*serverCell->readActorList\(ID_ACTOR_AI,\s*&writeActorList\);'
        )

        foreach ($requiredPattern in $requiredPatterns) {
            if (-not [regex]::IsMatch($Text, $requiredPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
                $Missing.Add($Name)
                return
            }
        }

        return
    }

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

$actorPacket = Get-SourceText "components\openmw-mp\Packets\Actor\ActorPacket.cpp"
$actorProcessorHeader = Get-SourceText "apps\openmw-mp\processors\ActorProcessor.hpp"
$actorProcessor = Get-SourceText "apps\openmw-mp\processors\ActorProcessor.cpp"
$baseStructs = Get-SourceText "components\openmw-mp\Base\BaseStructs.hpp"
$baseActor = Get-SourceText "components\openmw-mp\Base\BaseActor.hpp"
$actorStatsAuthority = Get-SourceText "components\openmw-mp\Base\ActorStatsAuthority.hpp"
$sequence = Get-SourceText "components\openmw-mp\Base\Sequence.hpp"
$basePacketTest = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$serverLuaCompatTest = Get-SourceText "apps\components_tests\openmw-mp\serverluacompat.cpp"
$actorPositionPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorPosition.cpp"
$actorAnimFlagsPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorAnimFlags.cpp"
$actorAnimPlayPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorAnimPlay.cpp"
$actorAttackPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorAttack.cpp"
$actorCastPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorCast.cpp"
$actorCellChangePacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorCellChange.cpp"
$actorAiPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorAI.cpp"
$actorEquipmentPacket = Get-SourceText "components\openmw-mp\Packets\Actor\PacketActorEquipment.cpp"
$clientActorList = Get-SourceText "apps\openmw\mwmp\ActorList.cpp"
$clientDedicatedActor = Get-SourceText "apps\openmw\mwmp\DedicatedActor.cpp"
$clientLocalActor = Get-SourceText "apps\openmw\mwmp\LocalActor.cpp"
$clientCellIdentity = Get-SourceText "apps\openmw\mwmp\CellIdentity.hpp"
$mechanicsActors = Get-SourceText "apps\openmw\mwmechanics\actors.cpp"
$characterController = Get-SourceText "apps\openmw\mwmechanics\character.cpp"
$clientCell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$clientCellController = Get-SourceText "apps\openmw\mwmp\CellController.cpp"
$clientPositionProcessor = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorPosition.hpp"
$clientCellChangeProcessor = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorCellChange.hpp"
$clientAiProcessor = Get-SourceText "apps\openmw\mwmp\processors\actor\ProcessorActorAI.hpp"
$actorSequenceCoalescing = Get-SourceText "apps\openmw-mp\processors\actor\ActorSequenceCoalescing.hpp"
$serverPositionProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorPosition.hpp"
$serverSimulation = Get-SourceText "apps\openmw-mp\ServerSimulation.cpp"
$serverSimulationHeader = Get-SourceText "apps\openmw-mp\ServerSimulation.hpp"
$serverAnimFlagsProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorAnimFlags.hpp"
$serverAnimPlayProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorAnimPlay.hpp"
$serverAttackProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorAttack.hpp"
$serverCastProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorCast.hpp"
$serverDeathProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorDeath.hpp"
$serverCellChangeProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorCellChange.hpp"
$serverAiProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorAI.hpp"
$serverEquipmentProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorEquipment.hpp"
$serverSpeechProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorSpeech.hpp"
$serverSpellsActiveProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorSpellsActive.hpp"
$serverStatsDynamicProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorStatsDynamic.hpp"
$serverActorListProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorList.hpp"
$serverActorTestProcessor = Get-SourceText "apps\openmw-mp\processors\actor\ProcessorActorTest.hpp"
$actorFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Actors.cpp"
$cellFunctionsHeader = Get-SourceText "apps\openmw-mp\Script\Functions\Cells.hpp"
$cellFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Cells.cpp"
$serverCellHeader = Get-SourceText "apps\openmw-mp\Cell.hpp"
$serverCppCell = Get-SourceText "apps\openmw-mp\Cell.cpp"
$serverCellControllerHeader = Get-SourceText "apps\openmw-mp\CellController.hpp"
$serverCellController = Get-SourceText "apps\openmw-mp\CellController.cpp"
$serverCMake = Get-SourceText "apps\openmw-mp\CMakeLists.txt"
$serverProcessorInitializer = Get-SourceText "apps\openmw-mp\processors\ProcessorInitializer.cpp"
$luaApiFunctions = Get-SourceText "scripts\data\tes3mp-lua-api-0.8.1-functions.txt"
$config = Get-SourceText "files\tes3mp\server\scripts\config.lua"
$eventHandler = Get-SourceText "files\tes3mp\server\scripts\eventHandler.lua"
$serverCell = Get-SourceText "files\tes3mp\server\scripts\cell\base.lua"
$logicHandler = Get-SourceText "files\tes3mp\server\scripts\logicHandler.lua"
$packetBuilder = Get-SourceText "files\tes3mp\server\scripts\packetBuilder.lua"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Server simulation source files are linked into the dedicated server target" -Text ($serverCMake + "`n" + $serverProcessorInitializer) `
    -Pattern '(?=.*set\(SERVER.*ServerSimulation\.cpp)(?=.*set\(SERVER_HEADER.*ServerSimulation\.hpp)(?=.*set\(PROCESSORS_OBJECT.*processors/object/ProcessorDoorDestination\.hpp)(?=.*add_executable\(tes3mp-server.*\$\{SERVER\}\s+\$\{SERVER_HEADER\}.*\$\{PROCESSORS_OBJECT\})(?=.*#include\s+"object/ProcessorDoorDestination\.hpp")(?=.*ObjectProcessor::AddProcessor\(new\s+ProcessorDoorDestination\(\)\))' `
    -Missing $missing

Test-Pattern -Name "Actor packets stay on the actor ordering channel with cell header and bounded actor count" -Text $actorPacket `
    -Pattern 'ActorPacket::ActorPacket\(\).*priority\s*=\s*PacketPriority::High;.*reliability\s*=\s*PacketReliability::ReliableOrdered;.*orderChannel\s*=\s*CHANNEL_ACTOR;.*bool\s+ActorPacket::PacketHeader\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!RW\(actorList->cell\.mData,\s*send,\s*true\)\s*\|\|\s*!RW\(actorList->cell\.mName,\s*send,\s*true\)\).*actorList->isValid\s*=\s*false.*actorList->count\s*=\s*\(unsigned\s+int\)\(actorList->baseActors\.size\(\)\);.*actorList->count\s*=\s*0;.*if\s*\(!RW\(actorList->count,\s*send\)\).*actorList->isValid\s*=\s*false.*if\s*\(actorList->count\s*>\s*maxActors\).*actorList->isValid\s*=\s*false' `
    -Missing $missing

Test-Pattern -Name "Actor packet headers reject named exterior cells" -Text ($actorPacket + "`n" + $basePacketTest) `
    -Pattern 'bool\s+ActorPacket::PacketHeader\(PacketStream\s*\*newBitstream,\s*bool\s+send\).*if\s*\(!send\s*&&\s*actorList->cell\.isExterior\(\)\s*&&\s*!actorList->cell\.mName\.empty\(\)\).*packetValid\s*=\s*false;.*actorList->isValid\s*=\s*false;.*return\s+false;.*actorPacketRejectsNamedExteriorCellHeader.*sent\.cell\.mData\.mX\s*=\s*-2;.*sent\.cell\.mData\.mY\s*=\s*-9;.*sent\.cell\.mName\s*=\s*"Seyda Neen";.*EXPECT_FALSE\(reader\.isPacketValid\(\)\);.*EXPECT_FALSE\(received\.isValid\);' `
    -Missing $missing

Test-Pattern -Name "Default server actor processor routes actor packets to loaded cell visitors" -Text ($actorProcessorHeader + "`n" + $actorProcessor) `
    -Pattern 'static\s+void\s+sendToLoaded\(ActorPacket\s*&\s*packet,\s*BaseActorList\s*&\s*actorList\).*void\s+ActorProcessor::Do\(ActorPacket\s*&\s*packet,\s*Player\s*&\s*player,\s*BaseActorList\s*&\s*actorList\)\s*\{\s*sendToLoaded\(packet,\s*actorList\);\s*\}.*void\s+ActorProcessor::sendToLoaded\(ActorPacket\s*&\s*packet,\s*BaseActorList\s*&\s*actorList\).*Cell\s*\*\s*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*==\s*nullptr.*return;.*packet\.setActorList\(&actorList\);.*serverCell->sendToLoaded\(&packet,\s*&actorList\);' `
    -Missing $missing

Test-Pattern -Name "Server actor cell-change packets route to source and destination loaded visitors" -Text ($actorProcessorHeader + "`n" + $actorProcessor + "`n" + $serverCellChangeProcessor + "`n" + $actorFunctions) `
    -Pattern 'static\s+void\s+sendCellChangeToLoaded\(ActorPacket\s*&\s*packet,\s*BaseActorList\s*&\s*actorList\).*void\s+sendToLoadedCellPlayers\(ActorPacket\s*&\s*packet,\s*BaseActorList\s*&\s*actorList,\s*Cell\s*\*\s*serverCell,\s*std::set<PacketGuid>&\s*sentGuids\).*pl\s*==\s*nullptr\s*\|\|\s*pl->npc\.mName\.empty\(\)\s*\|\|\s*pl->guid\s*==\s*actorList\.guid.*!sentGuids\.insert\(pl->guid\)\.second.*packet\.setActorList\(&actorList\);.*packet\.Send\(pl->guid\);.*void\s+ActorProcessor::sendCellChangeToLoaded\(ActorPacket\s*&\s*packet,\s*BaseActorList\s*&\s*actorList\).*std::set<PacketGuid>\s+sentGuids;.*sendToLoadedCellPlayers\(packet,\s*actorList,\s*CellController::get\(\)->getCell\(&actorList\.cell\),\s*sentGuids\);.*for\s*\(BaseActor&\s+actor\s*:\s*actorList\.baseActors\).*sendToLoadedCellPlayers\(packet,\s*actorList,\s*CellController::get\(\)->getCell\(&actor\.cell\),\s*sentGuids\);.*ProcessorActorCellChange.*sendCellChangeToLoaded\(packet,\s*actorList\);.*void\s+ActorFunctions::SendActorCellChange\(.*ActorProcessor::sendCellChangeToLoaded\(\*actorPacket,\s*writeActorList\);' `
    -Missing $missing

Test-Pattern -Name "Actor packet default state is deterministic for partial packet construction" -Text ($baseStructs + "`n" + $baseActor + "`n" + $basePacketTest) `
    -Pattern 'struct\s+Item.*int\s+count\s*=\s*0;.*int\s+charge\s*=\s*-1;.*float\s+enchantmentCharge\s*=\s*-1\.f;.*struct\s+ProjectileOrigin.*float\s+origin\[3\]\s*=\s*\{\};.*float\s+orientation\[4\]\s*=\s*\{\};.*struct\s+Target.*bool\s+isPlayer\s*=\s*false;.*unsigned\s+int\s+refNum\s*=\s*static_cast<unsigned\s+int>\(-1\);.*class\s+Attack.*char\s+type\s*=\s*MELEE;.*class\s+Cast.*char\s+type\s*=\s*0;.*bool\s+shouldSend\s*=\s*false;.*struct\s+SimpleCreatureStats.*bool\s+mDead\s*=\s*false;.*bool\s+mDeathAnimationFinished\s*=\s*false;.*class\s+BaseActor.*unsigned\s+int\s+refNum\s*=\s*0;.*unsigned\s+int\s+mpNum\s*=\s*0;.*unsigned\s+int\s+movementFlags\s*=\s*0;.*char\s+drawState\s*=\s*0;.*bool\s+isJumping\s*=\s*false;.*bool\s+isFlying\s*=\s*false;.*bool\s+hasCombatData\s*=\s*false;.*std::uint32_t\s+combatSequence\s*=\s*0;.*bool\s+isFollowerCellChange\s*=\s*false;.*bool\s+hasAiTarget\s*=\s*false;.*unsigned\s+int\s+aiAction\s*=\s*0;.*bool\s+hasPositionData\s*=\s*false;.*bool\s+hasAnimFlagsData\s*=\s*false;.*bool\s+hasEquipmentData\s*=\s*false;.*std::uint32_t\s+equipmentSequence\s*=\s*0;.*TEST\(MpBasePacketTest,\s*baseActorDefaultsAreSafeForPartialPacketConstruction\).*EXPECT_FALSE\(actor\.isJumping\);.*EXPECT_FALSE\(actor\.hasEquipmentData\);.*EXPECT_FALSE\(actor\.hasCombatData\);.*EXPECT_EQ\(actor\.combatSequence,\s*0u\);.*EXPECT_EQ\(actor\.equipmentSequence,\s*0u\);' `
    -Missing $missing

Test-Pattern -Name "Actor movement packets serialize position and direction and mark received position data" -Text $actorPositionPacket `
    -Pattern 'PacketActorPosition::PacketActorPosition\(\).*packetID\s*=\s*ID_ACTOR_POSITION;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*void\s+PacketActorPosition::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.positionSequence,\s*send\);.*RW\(actor\.position,\s*send,\s*true\);.*RW\(actor\.direction,\s*send,\s*true\);.*actor\.hasPositionData\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "Actor position packet coverage pins sequence, movement payload, and actor lane" -Text ($actorPositionPacket + "`n" + $basePacketTest) `
    -Pattern 'PacketActorPosition::PacketActorPosition\(\).*packetID\s*=\s*ID_ACTOR_POSITION;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*void\s+PacketActorPosition::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.positionSequence,\s*send\);.*RW\(actor\.position,\s*send,\s*true\);.*RW\(actor\.direction,\s*send,\s*true\);.*actor\.hasPositionData\s*=\s*true;.*actorPositionRoundTripsSequenceMovementAndMarksData.*actorPositionUsesUnreliableSequencedActorDelivery' `
    -Missing $missing

Test-Pattern -Name "Server actor position rejects send reliable accepted-position corrections to authority" -Text ($serverPositionProcessor + "`n" + $serverSimulation) `
    -Pattern 'std::vector<BaseActor>\s+correctionActors;.*std::map<ActorIdentityKey,\s*std::size_t>\s+correctionActorIndexes;.*addPositionCorrection.*acceptNewestPositionActor\(correctionActors,\s*correctionActorIndexes,\s*actor\);.*if\s*\(!isFiniteActorMovementSnapshot\(actor\)\).*addPositionCorrection\(\*currentActor\);.*if\s*\(!isClientActorControlUpdateAllowed\(currentActor\)\).*addPositionCorrection\(\*currentActor\);.*if\s*\(!hasNewerPosition\).*addPositionCorrection\(\*currentActor\);.*BaseActorList\s+correctionList\s*=\s*actorList;.*correctionList\.baseActors\s*=\s*correctionActors;.*packet\.setActorList\(&correctionList\);.*packet\.SendWithReliability\(actorList\.guid,\s*PacketReliability::ReliableOrdered\);' `
    -Missing $missing

Test-Pattern -Name "Actor animation play packets optionally carry movement snapshots with coverage" -Text ($actorAnimPlayPacket + "`n" + $clientLocalActor + "`n" + $clientCell + "`n" + $basePacketTest) `
    -Pattern 'PacketActorAnimPlay::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.combatSequence,\s*send\).*actor\.hasCombatData\s*=\s*true;.*RW\(actor\.hasPositionData,\s*send\);.*if\s*\(actor\.hasPositionData\).*RW\(actor\.positionSequence,\s*send\);.*RW\(actor\.position,\s*send,\s*true\);.*RW\(actor\.direction,\s*send,\s*true\);.*void\s+LocalActor::updateAnimPlay\(\).*if\s*\(!animation\.groupname\.empty\(\)\).*updatePosition\(true\);.*\+\+combatSequence;.*hasCombatData\s*=\s*true;.*addAnimPlayActor\(\*this\);.*void\s+Cell::readAnimPlay\(ActorList&\s+actorList\).*isActorCombatReplaySequenceAllowed\(\*actor,\s*baseActor\).*applySequencedPosition\(\*actor,\s*baseActor\).*acceptActorCombatReplaySequence\(\*actor,\s*baseActor\);.*actor->animation\.groupname\s*=\s*baseActor\.animation\.groupname;.*actor->playAnimation\(\);.*actorAnimPlayRoundTripsCombatTransformAndAnimation.*actorMovementAnimationPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Actor combat packets optionally carry movement snapshots with coverage" -Text ($actorAttackPacket + "`n" + $actorCastPacket + "`n" + $basePacketTest) `
    -Pattern 'PacketActorAttack::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.combatSequence,\s*send\).*actor\.hasCombatData\s*=\s*true;.*actor\.hasPositionData.*if\s*\(actor\.hasPositionData\).*actor\.positionSequence.*actor\.position.*actor\.direction.*PacketActorCast::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.combatSequence,\s*send\).*actor\.hasCombatData\s*=\s*true;.*actor\.hasPositionData.*if\s*\(actor\.hasPositionData\).*actor\.positionSequence.*actor\.position.*actor\.direction.*actorAttackRoundTripsCombatTransformAndHitState.*actorCastRoundTripsCombatTransformAndProjectileState' `
    -Missing $missing

Test-Pattern -Name "Actor combat events are sequenced before server fanout and client replay" -Text ($baseActor + "`n" + $actorAnimPlayPacket + "`n" + $actorAttackPacket + "`n" + $actorCastPacket + "`n" + $clientLocalActor + "`n" + $clientActorList + "`n" + $clientCell + "`n" + $actorSequenceCoalescing + "`n" + $serverSimulation + "`n" + $serverAnimPlayProcessor + "`n" + $serverAttackProcessor + "`n" + $serverCastProcessor + "`n" + $basePacketTest) `
    -Pattern '(?=.*isNewerActorCombatSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);)(?=.*bool\s+hasCombatData\s*=\s*false;)(?=.*std::uint32_t\s+combatSequence\s*=\s*0;)(?=.*isActorCombatSequenceAllowed\(const\s+BaseActor&\s+storedActor,\s*const\s+BaseActor&\s+incoming\).*isNewerActorCombatSequence\(incoming\.combatSequence,\s*storedActor\.combatSequence\))(?=.*acceptActorCombatSequence\(BaseActor&\s+storedActor,\s*const\s+BaseActor&\s+incoming\).*storedActor\.hasCombatData\s*=\s*true;.*storedActor\.combatSequence\s*=\s*incoming\.combatSequence;)(?=.*PacketActorAnimPlay::Actor.*RW\(actor\.combatSequence,\s*send\))(?=.*PacketActorAttack::Actor.*RW\(actor\.combatSequence,\s*send\))(?=.*PacketActorCast::Actor.*RW\(actor\.combatSequence,\s*send\))(?=.*void\s+LocalActor::updateAnimPlay\(\).*\+\+combatSequence;.*hasCombatData\s*=\s*true;.*addAnimPlayActor\(\*this\);)(?=.*void\s+LocalActor::updateAttackOrCast\(\).*if\s*\(attackReady\).*\+\+combatSequence;.*hasCombatData\s*=\s*true;.*addAttackActor\(\*this\);.*if\s*\(cast\.shouldSend\).*\+\+combatSequence;.*hasCombatData\s*=\s*true;.*addCastActor\(\*this\);)(?=.*acceptNewestCombatActor\(std::vector<BaseActor>&\s+actors,\s*const\s+BaseActor&\s+baseActor\).*isNewerActorCombatSequence\(baseActor\.combatSequence,\s*actor\.combatSequence\))(?=.*void\s+ActorList::addAnimPlayActor\(BaseActor\s+baseActor\).*acceptNewestCombatActor\(animPlayActors,\s*baseActor\);)(?=.*void\s+ActorList::addAttackActor\(BaseActor\s+baseActor\).*acceptNewestCombatActor\(attackActors,\s*baseActor\);)(?=.*void\s+ActorList::addCastActor\(BaseActor\s+baseActor\).*acceptNewestCombatActor\(castActors,\s*baseActor\);)(?=.*filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\).*isActorCombatSequenceAllowed\(\*currentActor,\s*actor\).*normalizeActorMovementSnapshot\(serverCell,\s*actor\).*requireMovement\s*&&\s*!actor\.hasPositionData.*acceptActorCombatSequence\(\*currentActor,\s*actor\);)(?=.*ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isActorCombatSequenceAllowed\(\*currentActor,\s*actor\).*isAcceptedActorAttackObservation\(serverCell,\s*\*currentActor,\s*actor\).*acceptActorCombatSequence\(\*currentActor,\s*actor\))(?=.*ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isActorCombatSequenceAllowed\(\*currentActor,\s*actor\).*isAcceptedActorCastObservation\(serverCell,\s*\*currentActor,\s*actor\).*acceptActorCombatSequence\(\*currentActor,\s*actor\))(?=.*ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\))(?=.*ProcessorActorAttack.*acceptActorAttacks\(actorList,\s*\*serverCell\))(?=.*ProcessorActorCast.*acceptActorCasts\(actorList,\s*\*serverCell\))(?=.*isActorCombatReplaySequenceAllowed\(const\s+DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*isActorCombatSequenceAllowed\(actor,\s*baseActor\))(?=.*acceptActorCombatReplaySequence\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*acceptActorCombatSequence\(actor,\s*baseActor\);)(?=.*void\s+Cell::readAttack\(ActorList&\s+actorList\).*isActorCombatReplaySequenceAllowed\(\*actor,\s*baseActor\).*normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\).*acceptActorCombatReplaySequence\(\*actor,\s*baseActor\);.*MechanicsHelper::processAttack)(?=.*void\s+Cell::readCast\(ActorList&\s+actorList\).*isActorCombatReplaySequenceAllowed\(\*actor,\s*baseActor\).*normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\).*acceptActorCombatReplaySequence\(\*actor,\s*baseActor\);.*MechanicsHelper::processCast)(?=.*actorCombatSequenceRejectsStaleEvents)' `
    -Missing $missing

Test-Pattern -Name "Actor animation flag packets are sequenced and preserve newer movement sidecars" -Text ($sequence + "`n" + $baseActor + "`n" + $actorAnimFlagsPacket + "`n" + $clientLocalActor + "`n" + $clientCell + "`n" + $actorSequenceCoalescing + "`n" + $serverAnimFlagsProcessor + "`n" + $serverCppCell + "`n" + $basePacketTest) `
    -Pattern '(?=.*bool\s+isNewerActorAnimFlagsSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);)(?=.*mergeNewestActorPosition\(BaseActor&\s+target,\s*const\s+BaseActor&\s+incoming\).*isNewerPositionSequence\(incoming\.positionSequence,\s*target\.positionSequence\).*)(?=.*mergeNewestActorAnimFlags\(BaseActor&\s+target,\s*const\s+BaseActor&\s+incoming\).*mergeNewestActorPosition\(target,\s*incoming\).*if\s*\(!incoming\.hasAnimFlagsData\).*return;.*isNewerActorAnimFlagsSequence\(incoming\.animFlagsSequence,\s*target\.animFlagsSequence\).*)(?=.*PacketActorAnimFlags::PacketActorAnimFlags\(\).*packetID\s*=\s*ID_ACTOR_ANIM_FLAGS;.*reliability\s*=\s*PacketReliability::UnreliableSequenced;.*RW\(actor\.hasPositionData,\s*send\).*RW\(actor\.animFlagsSequence,\s*send\).*actor\.hasAnimFlagsData\s*=\s*true;)(?=.*void\s+LocalActor::updateAnimFlags\(bool\s+forceUpdate\).*\+\+animFlagsSequence;.*getActorList\(\)->addAnimFlagsActor\(\*this\);)(?=.*void\s+Cell::readAnimFlags\(ActorList&\s+actorList\).*applySequencedPosition\(\*actor,\s*baseActor\);.*if\s*\(actor->hasAnimFlagsData\s*&&\s*!isNewerActorAnimFlagsSequence\(baseActor\.animFlagsSequence,\s*actor->animFlagsSequence\)\).*continue;.*actor->setAnimFlags\(\);)(?=.*filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*hasNewerPosition.*hasNewerAnimFlags.*acceptNewestAnimFlagsActor\(acceptedActors,\s*acceptedActorIndexes,\s*actor\);)(?=.*ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList\).*serverCell->sendToLoaded\(&packet,\s*&actorList\);)(?=.*case\s+ID_ACTOR_ANIM_FLAGS:.*actorToCache.*!isFiniteActorMovementSnapshot\(actorToCache\).*mergeNewestActorAnimFlags\(\*cellActor,\s*actorToCache\);)(?=.*actorAnimFlagsRoundTripsSequenceAndState)(?=.*actorAnimFlagsRoundTripsWithoutMovementSnapshot)(?=.*actorAnimFlagsMergeKeepsNewestMovementAndFlags)' `
    -Missing $missing

Test-Pattern -Name "Server actor animation flag rejects send reliable accepted-state corrections to authority" -Text ($actorSequenceCoalescing + "`n" + $serverAnimFlagsProcessor) `
    -Pattern '(?=.*std::vector<BaseActor>\*\s+correctionActors\s*=\s*nullptr)(?=.*std::map<ActorIdentityKey,\s*std::size_t>\s+correctionActorIndexes;)(?=.*acceptNewestAnimFlagsActor\(\*correctionActors,\s*correctionActorIndexes,\s*\*currentActor\);)(?=.*if\s*\(!hasNewerPosition\s*&&\s*!hasNewerAnimFlags\).*addAnimFlagsCorrection\(currentActor\);)(?=.*std::vector<BaseActor>\s+correctionActors;)(?=.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList,\s*&correctionActors\))(?=.*packet\.SendWithReliability\(actorList\.guid,\s*PacketReliability::ReliableOrdered\);)' `
    -Missing $missing

Test-Pattern -Name "Authority-owned local actors snapshot current movement input and transform-only changes" -Text $clientLocalActor `
    -Pattern 'void\s+LocalActor::updatePosition\(bool\s+forceUpdate\).*const\s+ESM::Position\s+ptrPosition\s*=\s*ptr\.getRefData\(\)\.getPosition\(\);.*const\s+MWMechanics::Movement&\s+movement\s*=\s*ptr\.getClass\(\)\.getMovementSettings\(ptr\);.*for\s*\(int\s+axis\s*=\s*0;\s*axis\s*<\s*3;\s*\+\+axis\).*direction\.pos\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mPosition\[axis\]\);.*direction\.rot\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mRotation\[axis\]\);.*const\s+float\s+transformEpsilon\s*=\s*0\.0001f;.*std::abs\(ptrPosition\.pos\[axis\]\s*-\s*position\.pos\[axis\]\)\s*>\s*transformEpsilon.*std::abs\(ptrPosition\.rot\[axis\]\s*-\s*position\.rot\[axis\]\)\s*>\s*transformEpsilon.*if\s*\(!creatureStats\.mDead\)\s*MechanicsHelper::deriveMissingMovementDirection\(direction,\s*ptrPosition,\s*position\);.*if\s*\(creatureStats\.mDead\).*posIsChanging\s*=\s*transformWasChanged;.*direction\.pos\[0\]\s*!=\s*0\s*\|\|\s*direction\.pos\[1\]\s*!=\s*0\s*\|\|\s*direction\.pos\[2\]\s*!=\s*0.*!MWBase::Environment::get\(\)\.getWorld\(\)->isOnGround\(ptr\)\s*\|\|\s*transformWasChanged;' `
    -Missing $missing

Test-Pattern -Name "Authority-owned local actor cadence preserves fractional timer remainder" -Text $clientCell `
    -Pattern '#include\s+<cmath>.*void\s+Cell::updateLocal\(bool\s+forceUpdate\).*const\s+float\s+timeoutSec\s*=\s*0\.025f;.*if\s*\(!forceUpdate\)\s*\{.*updateTimer\s*\+=\s*MWBase::Environment::get\(\)\.getFrameDuration\(\);.*if\s*\(updateTimer\s*<\s*timeoutSec\)\s*return;.*updateTimer\s*=\s*std::fmod\(updateTimer,\s*timeoutSec\);.*\}.*else\s+updateTimer\s*=\s*0;.*actor->update\(actor->hasSentData\s*\?\s*forceUpdate\s*:\s*true\);.*actorList->sendPositionActors\(\);.*actorList->sendAnimFlagsActors\(\);' `
    -Missing $missing

Test-Pattern -Name "Fresh local actors seed baseline position before transform-change comparisons" -Text $clientCell `
    -Pattern 'void\s+Cell::initializeLocalActor\(const\s+MWWorld::Ptr&\s+ptr\).*LocalActor\s+\*actor\s*=\s*new\s+LocalActor\(\);.*actor->cell\s*=\s*makeActorPacketCell\(\*store->getCell\(\)\);.*actor->setPtr\(ptr\);.*actor->position\s*=\s*ptr\.getRefData\(\)\.getPosition\(\);.*localActors\[mapIndex\]\s*=\s*actor;' `
    -Missing $missing

Test-Pattern -Name "Client actor packets canonicalize exterior cell identity by grid" -Text ($clientCellIdentity + "`n" + $clientActorList + "`n" + $clientLocalActor + "`n" + $clientCell + "`n" + $clientCellController) `
    -Pattern 'getCanonicalCellDescription\(const\s+ESM::Cell&\s+cell\).*if\s*\(cell\.isExterior\(\)\).*std::to_string\(cell\.mData\.mX\)\s*\+\s*", "\s*\+\s*std::to_string\(cell\.mData\.mY\).*makeActorPacketCell\(const\s+MWWorld::Cell&\s+cell\).*if\s*\(cell\.isExterior\(\)\).*packetCell\.mData\.mX\s*=\s*cell\.getGridX\(\);.*packetCell\.mData\.mY\s*=\s*cell\.getGridY\(\);.*packetCell\.mRegion\s*=\s*cell\.getRegion\(\);.*packetCell\.updateId\(\);.*void\s+ActorList::sendActorsInCell\(MWWorld::CellStore\*\s+cellStore\).*cell\s*=\s*makeActorPacketCell\(\*cellStore->getCell\(\)\);.*void\s+LocalActor::updateCell\(\).*cell\s*=\s*makeActorPacketCell\(\*ptr\.getCell\(\)->getCell\(\)\);.*void\s+Cell::updateLocal\(bool\s+forceUpdate\).*actorList->cell\s*=\s*makeActorPacketCell\(\*store->getCell\(\)\);.*std::string\s+Cell::getShortDescription\(\).*return\s+getCanonicalCellDescription\(makeActorPacketCell\(\*store->getCell\(\)\)\);.*std::string\s+cellDescription\(const\s+ESM::Cell&\s+cell\).*return\s+getCanonicalCellDescription\(cell\);' `
    -Missing $missing

Test-Pattern -Name "Client and server actor authority requires explicit assigned GUIDs" -Text ($serverCppCell + "`n" + $clientCell) `
    -Pattern 'Cell::Cell\(ESM::Cell\s+cell\).*:\s*cell\(cell\).*authorityGuid\(mwmp::unassignedPacketGuid\(\)\).*actorListRequestGuid\(mwmp::unassignedPacketGuid\(\)\).*bool\s+Cell::hasAuthority\(const\s+mwmp::PacketGuid&\s+guid\)\s+const\s*\{.*mwmp::isPacketGuidAssigned\(authorityGuid\)\s*&&\s*authorityGuid\s*==\s*guid;.*mwmp::Cell::Cell\(MWWorld::CellStore\*\s*cellStore\).*authorityGuid\s*=\s*mwmp::unassignedPacketGuid\(\);.*void\s+Cell::initializeLocalActor\(const\s+MWWorld::Ptr&\s+ptr\).*if\s*\(!hasLocalAuthority\(\)\).*return;.*void\s+Cell::initializeLocalActors\(\).*if\s*\(!hasLocalAuthority\(\)\).*return;.*bool\s+Cell::hasLocalAuthority\(\).*mwmp::isPacketGuidAssigned\(authorityGuid\)\s*&&\s*authorityGuid\s*==\s*Main::get\(\)\.getLocalPlayer\(\)->guid;' `
    -Missing $missing

Test-Pattern -Name "Server-authored actor movement prevents client authority proxy churn" -Text ($config + "`n" + $serverCell + "`n" + $logicHandler + "`n" + $clientCell + "`n" + $clientCellController) `
    -Pattern '(?=.*config\.serverAuthoritativeActors\s*=\s*false)(?=.*function\s+BaseCell:GetAuthority\(\).*config\.serverAuthoritativeActors\s*==\s*true.*return\s+nil)(?=.*function\s+BaseCell:SetAuthority\(pid\).*config\.serverAuthoritativeActors\s*==\s*true.*self\.authority\s*=\s*nil.*Skipped client actor authority.*return\s+true)(?=.*logicHandler\.LoadCellForPlayer\s*=\s*function\(pid,\s*cellDescription,\s*visitorOptions\).*config\.serverAuthoritativeActors\s*==\s*true.*LoadedCells\[cellDescription\]\.authority\s*=\s*nil.*return)(?=.*void\s+Cell::readPositions\(ActorList&\s+actorList\).*if\s*\(hasLocalAuthority\(\)\)\s*return;.*if\s*\(!mwmp::isPacketGuidAssigned\(actorList\.guid\)\)\s*setServerActorAuthority\(true\);)(?=.*bool\s+Cell::hasLocalAuthority\(\).*return\s+!serverActorAuthority\s*&&\s*mwmp::isPacketGuidAssigned\(authorityGuid\).*)(?=.*void\s+Cell::setAuthority\(const\s+PacketGuid&\s+guid\).*authorityGuid\s*=\s*guid;.*serverActorAuthority\s*=\s*false;)(?=.*void\s+Cell::setServerActorAuthority\(bool\s+enabled\).*authorityGuid\s*=\s*mwmp::unassignedPacketGuid\(\).*uninitializeLocalActors\(\);)' `
    -Missing $missing

Test-Pattern -Name "Actor cell-change packets serialize target cell, sequenced position, direction, and follower move flag" -Text ($actorCellChangePacket + "`n" + $basePacketTest) `
    -Pattern 'packetID\s*=\s*ID_ACTOR_CELL_CHANGE;.*RW\(actor\.cell\.mData,\s*send,\s*true\);.*RW\(actor\.cell\.mName,\s*send,\s*true\);.*RW\(actor\.positionSequence,\s*send\);.*RW\(actor\.position,\s*send,\s*true\);.*RW\(actor\.direction,\s*send,\s*true\);.*RW\(actor\.isFollowerCellChange,\s*send\);.*actor\.hasPositionData\s*=\s*true;.*actorCellChangeRoundTripsSequencedMovementSnapshot' `
    -Missing $missing

Test-Pattern -Name "Actor AI packets optionally carry movement snapshots and preserve package shapes" -Text ($actorAiPacket + "`n" + $clientCell + "`n" + $serverAiProcessor + "`n" + $basePacketTest) `
    -Pattern 'packetID\s*=\s*ID_ACTOR_AI;.*RW\(actor\.hasPositionData,\s*send\);.*if\s*\(actor\.hasPositionData\).*RW\(actor\.positionSequence,\s*send\);.*RW\(actor\.position,\s*send,\s*true\);.*RW\(actor\.direction,\s*send,\s*true\);.*RW\(actor\.aiAction,\s*send\);.*if\s*\(actor\.aiAction\s*!=\s*mwmp::BaseActorList::CANCEL\).*actor\.aiAction\s*==\s*mwmp::BaseActorList::WANDER.*RW\(actor\.aiDistance,\s*send\);.*RW\(actor\.aiShouldRepeat,\s*send\);.*actor\.aiAction\s*==\s*mwmp::BaseActorList::ESCORT\s*\|\|\s*actor\.aiAction\s*==\s*mwmp::BaseActorList::WANDER.*RW\(actor\.aiDuration,\s*send\);.*actor\.aiAction\s*==\s*mwmp::BaseActorList::ESCORT\s*\|\|\s*actor\.aiAction\s*==\s*mwmp::BaseActorList::TRAVEL.*RW\(actor\.aiCoordinates,\s*send\);.*actor\.aiAction\s*==\s*mwmp::BaseActorList::ACTIVATE.*actor\.aiAction\s*==\s*mwmp::BaseActorList::FOLLOW.*RW\(actor\.hasAiTarget,\s*send\);.*RW\(actor\.aiTarget\.isPlayer,\s*send\);.*RW\(actor\.aiTarget\.guid,\s*send\);.*RW\(actor\.aiTarget\.refId,\s*send,\s*true\);.*RW\(actor\.aiTarget\.refNum,\s*send\);.*RW\(actor\.aiTarget\.mpNum,\s*send\);.*void\s+Cell::readAi\(ActorList&\s+actorList\).*if\s*\(!applySequencedPosition\(\*actor,\s*baseActor\)\).*Ignoring ActorAI.*continue;.*actor->aiAction\s*=\s*baseActor\.aiAction;.*actor->setAi\(\);.*BPP_INIT\(ID_ACTOR_AI\).*serverCell->hasPlayer\(&player\).*acceptActorAiSnapshot\(actorList,\s*\*serverCell\).*actorAiRoundTripsMovementSnapshotAndTargetPackage.*actorAiRoundTripsWithoutMovementSnapshot.*actorCellAiStatsPacketsRejectTruncatedPayloads' `
    -Missing $missing

Test-Pattern -Name "Actor equipment packets are sequenced and cached authoritatively" -Text ($baseActor + "`n" + $actorEquipmentPacket + "`n" + $clientLocalActor + "`n" + $clientActorList + "`n" + $clientCell + "`n" + $actorSequenceCoalescing + "`n" + $serverEquipmentProcessor + "`n" + $serverCppCell + "`n" + $actorFunctions + "`n" + $basePacketTest) `
    -Pattern 'bool\s+isNewerActorEquipmentSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);.*bool\s+hasEquipmentData\s*=\s*false;.*std::uint32_t\s+equipmentSequence\s*=\s*0;.*bool\s+hasValidActorEquipment\(const\s+BaseActor&\s+actor\).*for\s*\(const\s+Item&\s+item\s*:\s*actor\.equipmentItems\).*isValidEquipmentItem\(item\).*void\s+PacketActorEquipment::Actor\(BaseActor\s+&actor,\s*bool\s+send\).*RW\(actor\.equipmentSequence,\s*send\).*isValidEquipmentItem\(equipmentItem\).*actor\.hasEquipmentData\s*=\s*true;.*void\s+LocalActor::updateEquipment\(bool\s+forceUpdate,\s*bool\s+sendImmediately\).*\+\+equipmentSequence;.*hasEquipmentData\s*=\s*true;.*acceptNewestEquipmentActor\(std::vector<BaseActor>&\s+actors,\s*const\s+BaseActor&\s+baseActor\).*isNewerActorEquipmentSequence\(baseActor\.equipmentSequence,\s*actor\.equipmentSequence\).*void\s+ActorList::addEquipmentActor\(BaseActor\s+baseActor\).*acceptNewestEquipmentActor\(equipmentActors,\s*baseActor\);.*void\s+Cell::readEquipment\(ActorList&\s+actorList\).*actor->hasEquipmentData.*!isNewerActorEquipmentSequence\(baseActor\.equipmentSequence,\s*actor->equipmentSequence\).*actor->hasEquipmentData\s*=\s*true;.*actor->equipmentSequence\s*=\s*baseActor\.equipmentSequence;.*filterActorEquipmentToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*isClientActorControlUpdateAllowed\(currentActor\).*hasValidActorEquipment\(actor\).*isNewerActorEquipmentSequence\(actor\.equipmentSequence,\s*currentActor->equipmentSequence\).*ProcessorActorEquipment.*filterActorEquipmentToServerAccepted\(serverCell,\s*actorList\).*serverCell->readActorList\(packetID,\s*&actorList\);.*case\s+ID_ACTOR_EQUIPMENT:.*isNewerActorEquipmentSequence\(newActor\.equipmentSequence,\s*cellActor->equipmentSequence\).*hasValidActorEquipment\(newActor\).*cellActor->hasEquipmentData\s*=\s*true;.*cellActor->equipmentSequence\s*=\s*newActor\.equipmentSequence;.*advanceActorEquipmentSequences\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*actor\.equipmentSequence\s*=\s*storedActor->equipmentSequence\s*\+\s*1;.*actor\.hasEquipmentData\s*=\s*true;.*void\s+ActorFunctions::SendActorEquipment\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*advanceActorEquipmentSequences\(serverCell,\s*writeActorList\).*serverCell->readActorList\(ID_ACTOR_EQUIPMENT,\s*&writeActorList\);.*actorEquipmentRoundTripsSequenceAndItems' `
    -Missing $missing

Test-Pattern -Name "Client helper ActorAI packets mark target-based packages as targeted" -Text $clientActorList `
    -Pattern 'void\s+ActorList::addAiActor\(const\s+MWWorld::Ptr&\s+actorPtr,\s*const\s+MWWorld::Ptr&\s+targetPtr,\s*unsigned\s+int\s+aiAction\).*mwmp::BaseActor\s+baseActor;.*CellController\*\s+cellController\s*=\s*Main::get\(\)\.getCellController\(\);.*const\s+auto\s+\[refNum,\s*mpNum\]\s*=\s*cellController->getActorNetworkId\(actorPtr\);.*baseActor\.refNum\s*=\s*refNum;.*baseActor\.mpNum\s*=\s*mpNum;.*if\s*\(cellController->isLocalActor\(actorPtr\)\).*LocalActor\*\s+localActor\s*=\s*cellController->getLocalActor\(actorPtr\);.*localActor->updatePosition\(true\);.*baseActor\s*=\s*\*localActor;.*baseActor\.aiAction\s*=\s*aiAction;.*baseActor\.aiTarget\s*=\s*MechanicsHelper::getTarget\(targetPtr\);.*baseActor\.hasAiTarget\s*=\s*!MechanicsHelper::isEmptyTarget\(baseActor\.aiTarget\);.*addAiActor\(baseActor\);' `
    -Missing $missing

Test-Pattern -Name "Authority-owned local actors enqueue movement, animation, AI, attack/cast, and cell-change state" -Text ($clientLocalActor + "`n" + $clientActorList + "`n" + $clientCell) `
    -Pattern 'void\s+LocalActor::update\(bool\s+forceUpdate\).*updatePosition\(forceUpdate\);.*updateAnimFlags\(forceUpdate\);.*updateAnimPlay\(\);.*updateSpeech\(\);.*updateAttackOrCast\(\);.*void\s+LocalActor::updateCell\(\).*cell\s*=\s*makeActorPacketCell\(\*ptr\.getCell\(\)->getCell\(\)\);.*position\s*=\s*ptr\.getRefData\(\)\.getPosition\(\);.*direction\.pos\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mPosition\[axis\]\);.*direction\.rot\[axis\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(movement\.mRotation\[axis\]\);.*\+\+positionSequence;.*hasPositionData\s*=\s*true;.*isFollowerCellChange\s*=\s*false;.*getActorList\(\)->addCellChangeActor\(\*this\);.*void\s+LocalActor::updatePosition\(bool\s+forceUpdate\).*position\s*=\s*ptr\.getRefData\(\)\.getPosition\(\);.*\+\+positionSequence;.*getActorList\(\)->addPositionActor\(\*this\);.*void\s+ActorList::sendPositionActors\(\).*ID_ACTOR_POSITION.*void\s+ActorList::sendAiActors\(\).*ID_ACTOR_AI.*void\s+ActorList::sendCellChangeActors\(\).*ID_ACTOR_CELL_CHANGE.*actorList->sendPositionActors\(\);.*actorList->sendAnimFlagsActors\(\);.*actorList->sendAiActors\(\);.*actorList->sendCellChangeActors\(\);' `
    -Missing $missing

Test-Pattern -Name "Authority-owned local actor movement queues keep newest movement and anim flag sequences per actor" -Text $clientActorList `
    -Pattern 'bool\s+hasSameActorIdentity\(const\s+BaseActor&\s+left,\s*const\s+BaseActor&\s+right\).*left\.refNum\s*==\s*right\.refNum\s*&&\s*left\.mpNum\s*==\s*right\.mpNum.*acceptNewestPositionActor\(std::vector<BaseActor>&\s+actors,\s*const\s+BaseActor&\s+baseActor\).*if\s*\(!hasSameActorIdentity\(actor,\s*baseActor\)\).*continue;.*if\s*\(isNewerPositionSequence\(baseActor\.positionSequence,\s*actor\.positionSequence\)\)\s*actor\s*=\s*baseActor;.*actors\.push_back\(baseActor\);.*acceptNewestAnimFlagsActor\(std::vector<BaseActor>&\s+actors,\s*const\s+BaseActor&\s+baseActor\).*mergeNewestActorAnimFlags\(actor,\s*baseActor\);.*void\s+ActorList::addPositionActor\(BaseActor\s+baseActor\).*acceptNewestPositionActor\(positionActors,\s*baseActor\);.*void\s+ActorList::addAnimFlagsActor\(BaseActor\s+baseActor\).*acceptNewestAnimFlagsActor\(animFlagsActors,\s*baseActor\);' `
    -Missing $missing

Test-Pattern -Name "Local actor attack and cast packets do not defer each other in the same update" -Text $clientLocalActor `
    -Pattern 'void\s+LocalActor::updateAttackOrCast\(\).*const\s+bool\s+attackReady\s*=\s*attack\.shouldSend\s*&&\s*!MechanicsHelper::shouldDeferLocalAttack\(attack\);.*if\s*\(attackReady\s*\|\|\s*cast\.shouldSend\)\s*updatePosition\(true\);.*if\s*\(attackReady\).*getActorList\(\)->addAttackActor\(\*this\);.*attack\.shouldSend\s*=\s*false;.*if\s*\(cast\.shouldSend\).*getActorList\(\)->addCastActor\(\*this\);.*cast\.shouldSend\s*=\s*false;.*cast\.hasProjectile\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Client processors route incoming movement, cell changes, and AI into CellController" -Text ($clientPositionProcessor + "`n" + $clientCellChangeProcessor + "`n" + $clientAiProcessor + "`n" + $clientCellController) `
    -Pattern 'BPP_INIT\(ID_ACTOR_POSITION\).*readPositions\(actorList\);.*BPP_INIT\(ID_ACTOR_CELL_CHANGE\).*readCellChange\(actorList\);.*BPP_INIT\(ID_ACTOR_AI\).*readAi\(actorList\);.*void\s+CellController::readPositions\(ActorList&\s+actorList\).*initializeCell\(actorList\.cell\);.*cellsInitialized\[mapIndex\]->readPositions\(actorList\);.*void\s+CellController::readAi\(ActorList&\s+actorList\).*initializeCell\(actorList\.cell\);.*cellsInitialized\[mapIndex\]->readAi\(actorList\);.*void\s+CellController::readCellChange\(ActorList&\s+actorList\).*initializeCell\(actorList\.cell\);.*cellsInitialized\[mapIndex\]->readCellChange\(actorList\);' `
    -Missing $missing

Test-Pattern -Name "Client live actor animation and combat packets initialize missing dedicated actors before applying" -Text $clientCell `
    -Pattern 'bool\s+applySequencedPosition\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*if\s*\(!baseActor\.hasPositionData\).*return\s+true;.*if\s*\(actor\.hasPositionData\s*&&\s*!isNewerPositionSequence\(baseActor\.positionSequence,\s*actor\.positionSequence\)\).*return\s+false;.*actor\.position\s*=\s*baseActor\.position;.*actor\.direction\s*=\s*baseActor\.direction;.*actor\.positionSequence\s*=\s*baseActor\.positionSequence;.*actor\.hasPositionData\s*=\s*true;.*actor\.setPosition\(\);.*return\s+true;.*bool\s+normalizeSequencedPositionForCombat\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*if\s*\(!baseActor\.hasPositionData\)\s*return\s+false;.*if\s*\(actor\.hasPositionData\s*&&\s*!isNewerPositionSequence\(baseActor\.positionSequence,\s*actor\.positionSequence\)\)\s*return\s+true;.*return\s+applySequencedPosition\(actor,\s*baseActor\);.*void\s+Cell::readAnimFlags\(ActorList&\s+actorList\).*initializeDedicatedActors\(latestActorList\);.*if\s*\(dedicatedActors\.empty\(\)\)\s*return;.*actor->movementFlags\s*=\s*baseActor\.movementFlags;.*actor->drawState\s*=\s*baseActor\.drawState;.*actor->isJumping\s*=\s*baseActor\.isJumping;.*actor->isFlying\s*=\s*baseActor\.isFlying;.*actor->setAnimFlags\(\);.*if\s*\(hasLocalAuthority\(\)\).*uninitializeDedicatedActors\(latestActorList\);.*void\s+Cell::readAnimPlay\(ActorList&\s+actorList\).*initializeDedicatedActors\(actorList\);.*if\s*\(dedicatedActors\.empty\(\)\)\s*return;.*isActorCombatReplaySequenceAllowed\(\*actor,\s*baseActor\).*if\s*\(!applySequencedPosition\(\*actor,\s*baseActor\)\).*continue;.*acceptActorCombatReplaySequence\(\*actor,\s*baseActor\);.*actor->animation\.groupname\s*=\s*baseActor\.animation\.groupname;.*actor->playAnimation\(\);.*if\s*\(hasLocalAuthority\(\)\).*uninitializeDedicatedActors\(actorList\);.*void\s+Cell::readAttack\(ActorList&\s+actorList\).*initializeDedicatedActors\(actorList\);.*if\s*\(dedicatedActors\.empty\(\)\)\s*return;.*if\s*\(!normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\)\).*continue;.*actor->attack\s*=\s*baseActor\.attack;.*MechanicsHelper::processAttack\(actor->attack,\s*actor->getPtr\(\),\s*false\);.*if\s*\(hasLocalAuthority\(\)\).*uninitializeDedicatedActors\(actorList\);.*void\s+Cell::readCast\(ActorList&\s+actorList\).*initializeDedicatedActors\(actorList\);.*if\s*\(dedicatedActors\.empty\(\)\)\s*return;.*if\s*\(!normalizeSequencedPositionForCombat\(\*actor,\s*baseActor\)\).*continue;.*actor->cast\s*=\s*baseActor\.cast;.*actor->setAnimFlags\(\);.*MechanicsHelper::processCast\(actor->cast,\s*actor->getPtr\(\),\s*false\);.*if\s*\(hasLocalAuthority\(\)\).*uninitializeDedicatedActors\(actorList\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor movement derives missing animation direction from accepted transforms" -Text $clientCell `
    -Pattern 'bool\s+applySequencedPosition\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*const\s+bool\s+hadPositionData\s*=\s*actor\.hasPositionData;.*const\s+ESM::Position\s+previousPosition\s*=\s*actor\.position;.*actor\.position\s*=\s*baseActor\.position;.*actor\.direction\s*=\s*baseActor\.direction;.*actor\.positionSequence\s*=\s*baseActor\.positionSequence;.*actor\.hasPositionData\s*=\s*true;.*if\s*\(hadPositionData\)\s*MechanicsHelper::deriveMissingMovementDirection\(actor\.direction,\s*actor\.position,\s*previousPosition\);.*if\s*\(!hadPositionData\)\s*actor\.setPosition\(\);.*else.*actor\.move\(immediateReplayStep\);' `
    -Missing $missing

Test-Pattern -Name "Client actor movement batches replay newest movement and anim flag sequences per actor" -Text $clientCell `
    -Pattern 'coalesceNewestPositionActors\(const\s+std::vector<BaseActor>&\s+baseActors\).*std::map<std::string,\s*std::size_t>\s+actorIndexes;.*generateMapIndex\(baseActor\).*if\s*\(isNewerPositionSequence\(baseActor\.positionSequence,\s*acceptedActor\.positionSequence\)\)\s*acceptedActor\s*=\s*baseActor;.*coalesceNewestAnimFlagsActors\(const\s+std::vector<BaseActor>&\s+baseActors\).*mergeNewestActorAnimFlags\(acceptedActor,\s*baseActor\);.*void\s+Cell::readPositions\(ActorList&\s+actorList\).*latestActorList\.baseActors\s*=\s*coalesceNewestPositionActors\(actorList\.baseActors\);.*void\s+Cell::readAnimFlags\(ActorList&\s+actorList\).*latestActorList\.baseActors\s*=\s*coalesceNewestAnimFlagsActors\(actorList\.baseActors\);.*applySequencedPosition\(\*actor,\s*baseActor\);.*if\s*\(actor->hasAnimFlagsData\s*&&\s*!isNewerActorAnimFlagsSequence\(baseActor\.animFlagsSequence,\s*actor->animFlagsSequence\)\).*continue;.*uninitializeDedicatedActors\(latestActorList\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor packet cleanup skips actors that were not initialized locally" -Text $clientCell `
    -Pattern 'void\s+Cell::uninitializeDedicatedActors\(ActorList&\s+actorList\).*std::string\s+mapIndex\s*=\s*Main::get\(\)\.getCellController\(\)->generateMapIndex\(baseActor\);.*auto\s+found\s*=\s*dedicatedActors\.find\(mapIndex\);.*if\s*\(found\s*==\s*dedicatedActors\.end\(\)\)\s*continue;.*removeDedicatedActorRecord\(mapIndex\);.*delete\s+found->second;.*dedicatedActors\.erase\(found\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor cell changes drop invalid destination cells without dereferencing them" -Text $clientCell `
    -Pattern 'void\s+Cell::readCellChange\(ActorList&\s+actorList\).*dedicatedActor->positionSequence\s*=\s*baseActor\.positionSequence;.*dedicatedActor->hasPositionData\s*=\s*true;.*MWWorld::CellStore\s+\*newStore\s*=\s*cellController->getCellStore\(dedicatedActor->cell\);.*if\s*\(!newStore\).*Destination cell doesn''t exist on this client.*getWorld\(\)->disable\(dedicatedActor->getPtr\(\)\);.*removeDedicatedActorRecord\(mapIndex\);.*delete\s+dedicatedActor;.*dedicatedActors\.erase\(mapIndex\);.*continue;.*dedicatedActor->setCell\(newStore\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actors apply first or newer actor position packets only" -Text ($baseActor + "`n" + $sequence + "`n" + $clientCell + "`n" + $basePacketTest) `
    -Pattern 'bool\s+isNewerPositionSequence\(std::uint32_t\s+incoming,\s*std::uint32_t\s+current\).*return\s+isNewerSequence\(incoming,\s*current\);.*bool\s+applySequencedPosition\(DedicatedActor&\s+actor,\s*const\s+BaseActor&\s+baseActor\).*if\s*\(!baseActor\.hasPositionData\).*return\s+true;.*if\s*\(!isFinitePosition\(baseActor\.position\)\s*\|\|\s*!isFinitePosition\(baseActor\.direction\)\).*return\s+false;.*if\s*\(actor\.hasPositionData\s*&&\s*!isNewerPositionSequence\(baseActor\.positionSequence,\s*actor\.positionSequence\)\).*return\s+false;.*actor\.position\s*=\s*baseActor\.position;.*actor\.direction\s*=\s*baseActor\.direction;.*actor\.positionSequence\s*=\s*baseActor\.positionSequence;.*actor\.hasPositionData\s*=\s*true;.*if\s*\(!hadPositionData\)\s*actor\.setPosition\(\);.*else.*actor\.move\(immediateReplayStep\);.*return\s+true;.*void\s+Cell::readPositions\(ActorList&\s+actorList\).*initializeDedicatedActors\(latestActorList\);.*applySequencedPosition\(\*actor,\s*baseActor\);.*localActor->positionSequence\s*=\s*dedicatedActor->positionSequence;.*movementSequenceHelpersHandleWrapAroundBoundaries' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor update waits for first authoritative position and animation flags" -Text ($baseActor + "`n" + $clientDedicatedActor) `
    -Pattern 'BaseActor\(\).*hasPositionData\s*=\s*false;.*hasAnimFlagsData\s*=\s*false;.*void\s+DedicatedActor::update\(float\s+dt\).*if\s*\(hasPositionData\)\s*\{.*move\(dt\);.*\}.*if\s*\(hasAnimFlagsData\)\s*setAnimFlags\(\);.*setStatsDynamic\(\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actors replay serialized actor jumping through ForceJump animation state" -Text ($baseActor + "`n" + $clientDedicatedActor) `
    -Pattern 'class\s+BaseActor.*bool\s+isJumping\s*=\s*false;.*void\s+DedicatedActor::setAnimFlags\(\).*ptrCreatureStats->setMovementFlag\(CreatureStats::Flag_ForceJump,\s*isJumping\s*\|\|\s*\(movementFlags\s*&\s*CreatureStats::Flag_ForceJump\)\s*!=\s*0\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor snaps and cell moves immediately apply movement settings and rotation" -Text $clientDedicatedActor `
    -Pattern '(?=.*void\s+DedicatedActor::setCell\(MWWorld::CellStore\s+\*cellStore\).*ptr\s*=\s*world->moveObject\(ptr,\s*cellStore,\s*position\.asVec3\(\)\);.*setMovementSettings\(\);.*world->rotateObject\(ptr,\s*position\.asRotationVec3\(\)\);)(?=.*void\s+DedicatedActor::move\(float\s+dt\).*const\s+ESM::Position\s+previousVisualPosition\s*=.*world->moveObject\(ptr,\s*refPos\.asVec3\(\)\);.*world->rotateObject\(ptr,\s*position\.asRotationVec3\(\)\);.*setMovementSettingsFromVisualDelta\(previousVisualPosition\);.*else\s*\{\s*setPosition\(\);)(?=.*void\s+DedicatedActor::setPosition\(\).*world->moveObject\(ptr,\s*position\.asVec3\(\)\);.*setMovementSettings\(\);.*world->rotateObject\(ptr,\s*position\.asRotationVec3\(\)\);)(?=.*void\s+DedicatedActor::setMovementSettingsFromVisualDelta\(const\s+ESM::Position&\s+previousPosition\).*MechanicsHelper::deriveMissingMovementDirection\(animationDirection,\s*ptr\.getRefData\(\)\.getPosition\(\),\s*previousPosition\);.*if\s*\(animationDirection\.pos\[0\]\s*==\s*0\.f\s*&&\s*animationDirection\.pos\[1\]\s*==\s*0\.f\).*animationDirection\.pos\[0\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[0\]\);.*animationDirection\.pos\[1\]\s*=\s*MechanicsHelper::sanitizeMovementComponent\(direction\.pos\[1\]\);.*setMovementSettings\(animationDirection\);).*' `
    -Missing $missing

Test-Pattern -Name "Dedicated actor movement input drives animation without local physics drift" -Text $characterController `
    -Pattern 'bool\s+isDedicatedMultiplayerProxy\(const\s+MWWorld::Ptr&\s+ptr\).*mwmp::PlayerList::isDedicatedPlayer\(ptr\).*mwmp::Main::get\(\)\.getCellController\(\)->isDedicatedActor\(ptr\).*const\s+bool\s+dedicatedMultiplayerProxy\s*=\s*isDedicatedMultiplayerProxy\(mPtr\);.*if\s*\(!dedicatedMultiplayerProxy\s*&&\s*!isKnockedDown\(\)\s*&&\s*!isKnockedOut\(\)\).*world->rotateObject\(mPtr,\s*rot,\s*true\);.*else\s+if\s*\(!dedicatedMultiplayerProxy\).*world->rotateObject\(mPtr,\s*rot,\s*true\);.*if\s*\(dedicatedMultiplayerProxy\)\s*movement\s*=\s*osg::Vec3f\(\);.*world->queueMovement\(mPtr,\s*movement\);' `
    -Missing $missing

Test-Pattern -Name "Network-driven multiplayer actors do not run local AI steering" -Text $mechanicsActors `
    -Pattern 'bool\s+isNetworkDrivenMultiplayerActor\(const\s+MWWorld::Ptr&\s+ptr\).*mwmp::PlayerList::isDedicatedPlayer\(ptr\).*mwmp::Main::get\(\)\.getCellController\(\)->isDedicatedActor\(ptr\).*void\s+Actors::predictAndAvoidCollisions\(float\s+duration\).*if\s*\(isNetworkDrivenMultiplayerActor\(ptr\)\)\s*continue;.*void\s+Actors::update\(float\s+duration,\s*bool\s+paused\).*const\s+bool\s+networkDrivenActor\s*=\s*isNetworkDrivenMultiplayerActor\(actor\.getPtr\(\)\);.*if\s*\(!networkDrivenActor\s*&&\s*aiActive\s*&&\s*inProcessingRange\).*stats\.getAiSequence\(\)\.execute\(actor\.getPtr\(\),\s*ctrl,\s*duration\);.*else\s+if\s*\(!networkDrivenActor\s*&&\s*aiActive\s*&&\s*!isPlayer\s*&&\s*isConscious\(actor\.getPtr\(\)\).*stats\.getAiSequence\(\)\.execute\(actor\.getPtr\(\),\s*ctrl,\s*duration,\s*/\*outOfRange\*/\s*true\);.*if\s*\(!networkDrivenActor\s*&&\s*inProcessingRange\s*&&\s*actor\.getPtr\(\)\.getClass\(\)\.isNpc\(\)\).*updateDrowning\(actor\.getPtr\(\),\s*duration,\s*ctrl\.isKnockedOut\(\),\s*isPlayer\);.*if\s*\(!networkDrivenActor\s*&&\s*luaControls\s*!=\s*nullptr\s*&&\s*isConscious\(actor\.getPtr\(\)\)\).*updateLuaControls\(actor\.getPtr\(\),\s*isPlayer,\s*\*luaControls\);' `
    -Missing $missing

Test-Pattern -Name "Server accepts actor position observations from loaded clients and forwards canonical state" -Text ($serverPositionProcessor + "`n" + $serverSimulation) `
    -Pattern 'BPP_INIT\(ID_ACTOR_POSITION\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasPlayer\(&player\).*getServerSimulation\(\)\.acceptActorMovementSnapshot\(packet,\s*actorList,\s*\*serverCell\);.*std::vector<BaseActor>\s+acceptedActors;.*BaseActor\*\s+currentActor\s*=\s*serverCell\.getActor\(actor\.refNum,\s*actor\.mpNum\);.*isClientActorControlUpdateAllowed\(currentActor\).*isNewerPositionSequence\(actor\.positionSequence,\s*currentActor->positionSequence\).*simulateMovementPosition\(\s*currentActor->position,\s*actor\.position,\s*simulatedActor\.direction,\s*deltaSeconds\).*actorList\.baseActors\s*=\s*acceptedActors;.*actorList\.count\s*=\s*static_cast<unsigned\s+int>\(actorList\.baseActors\.size\(\)\);.*if\s*\(actorList\.count\s*==\s*0\)\s*return\s+false;.*serverCell\.readActorList\(ID_ACTOR_POSITION,\s*&actorList\);.*serverCell\.sendToLoaded\(&packet,\s*&actorList\);' `
    -Missing $missing

Test-Pattern -Name "Server simulation ticks loaded actor movement from cached intent" -Text ($serverSimulation + "`n" + $serverCellHeader + "`n" + $serverCellControllerHeader + "`n" + $serverCellController) `
    -Pattern 'void\s+ServerSimulation::tick\(\).*mActorTickAccumulator\s*\+=\s*deltaSeconds;.*mActorTickAccumulator\s*<\s*actorTickIntervalSeconds.*tickActors\(actorDeltaSeconds\);.*void\s+ServerSimulation::tickActors\(float\s+deltaSeconds\).*CellController\*\s+cellController\s*=\s*CellController::get\(\);.*Networking::get\(\)\.getActorPacketController\(\)->GetPacket\(ID_ACTOR_POSITION\);.*for\s*\(Cell\*\s+cell\s*:\s*cellController->getCells\(\)\).*BaseActorList\s+tickActorList;.*tickActorList\.cell\s*=\s*cell->getCellData\(\);.*tickActorList\.guid\s*=\s*unassignedPacketGuid\(\);.*for\s*\(BaseActor&\s+actor\s*:\s*storedActorList->baseActors\).*isClientActorControlUpdateAllowed\(&actor\).*hasMovementIntent\(direction\).*simulateMovementPosition\(actor\.position,\s*actor\.position,\s*direction,\s*deltaSeconds\).*actor\.direction\s*=\s*direction;.*\+\+actor\.positionSequence;.*tickActorList\.baseActors\.push_back\(actor\);.*cell->sendToLoaded\(actorPacket,\s*&tickActorList\);.*const\s+ESM::Cell&\s+getCellData\(\)\s+const;.*const\s+TContainer&\s+getCells\(\)\s+const;.*const\s+CellController::TContainer&\s+CellController::getCells\(\)\s+const.*return\s+cells;' `
    -Missing $missing

Test-Pattern -Name "Server simulation ticks only active or server-interested cells" -Text ($serverSimulation + "`n" + $serverCellHeader + "`n" + $serverCppCell + "`n" + $serverCellController + "`n" + $cellFunctionsHeader + "`n" + $cellFunctions + "`n" + $luaApiFunctions) `
    -Pattern '(?=.*void\s+ServerSimulation::tickActors\(float\s+deltaSeconds\).*for\s*\(Cell\*\s+cell\s*:\s*cellController->getCells\(\)\).*if\s*\(!cell->hasPlayers\(\)\s*&&\s*!cell->hasSimulationInterest\(\)\)\s*continue;.*BaseActorList\*\s+storedActorList\s*=\s*cell->getActorList\(\))(?=.*bool\s+hasPlayers\(\)\s+const;.*bool\s+hasSimulationInterest\(\)\s+const;.*void\s+setSimulationInterest\(bool\s+enabled\);)(?=.*bool\s+Cell::hasPlayers\(\)\s+const.*return\s+!players\.empty\(\);.*bool\s+Cell::hasSimulationInterest\(\)\s+const.*return\s+simulationInterest;.*void\s+Cell::setSimulationInterest\(bool\s+enabled\).*simulationInterest\s*=\s*enabled;)(?=.*if\s*\(!c->hasPlayers\(\)\s*&&\s*!c->hasSimulationInterest\(\)\)\s*toDelete\.push_back\(c\);)(?=.*GetCellSimulationInterest.*CellFunctions::GetCellSimulationInterest)(?=.*SetCellSimulationInterest.*CellFunctions::SetCellSimulationInterest)(?=.*bool\s+CellFunctions::GetCellSimulationInterest\(const\s+char\s+\*cellDescription\).*CellController::get\(\)->getCell\(&esmCell\).*return\s+cell\s*!=\s*nullptr\s*&&\s*cell->hasSimulationInterest\(\);)(?=.*void\s+CellFunctions::SetCellSimulationInterest\(const\s+char\s+\*cellDescription,\s*bool\s+enabled\).*enabled\s*\?\s*CellController::get\(\)->addCell\(esmCell\)\s*:\s*CellController::get\(\)->getCell\(&esmCell\).*cell->setSimulationInterest\(enabled\);)(?=.*GetCellSimulationInterest)(?=.*SetCellSimulationInterest)' `
    -Missing $missing

Test-Pattern -Name "Server simulation derives actor movement from cached AI packages" -Text ($serverSimulationHeader + "`n" + $serverSimulation + "`n" + $actorFunctions) `
    -Pattern '(?=.*constexpr\s+float\s+aiCoordinateStopDistance\s*=\s*64\.f;)(?=.*constexpr\s+float\s+aiTargetStopDistance\s*=\s*128\.f;)(?=.*bool\s+isLivePlayerAiTarget\(const\s+Player&\s+player\).*player\.creatureStats\.mDead.*player\.hasFiniteDynamicStats\(\).*player\.creatureStats\.mDynamic\[0\]\.mCurrent\s*<=\s*healthDeadEpsilon)(?=.*bool\s+getAiTargetPosition\(Cell&\s+cell,\s*const\s+mwmp::Target&\s+target,\s*ESM::Position&\s+destination\).*target\.isPlayer.*Players::getPlayer\(target\.guid\).*player->hasFinitePositionPacket\(\).*isLivePlayerAiTarget\(\*player\).*destination\s*=\s*player->position.*findActorTarget\(cell,\s*target\).*actor->hasPositionData.*destination\s*=\s*actor->position)(?=.*float\s+getAiStopDistance\(const\s+mwmp::BaseActor&\s+actor,\s*bool\s+coordinatePackage\).*coordinatePackage.*aiCoordinateStopDistance.*actor\.aiDistance\s*==\s*0.*aiTargetStopDistance.*std::clamp\(static_cast<float>\(actor\.aiDistance\),\s*aiMinimumStopDistance,\s*aiMaximumStopDistance\))(?=.*bool\s+buildAiMovementIntent\(Cell&\s+cell,\s*mwmp::BaseActor&\s+actor,\s*ESM::Position&\s+direction\).*actor\.hasAiData.*actor\.aiAction.*mwmp::BaseActorList::TRAVEL.*mwmp::BaseActorList::ESCORT.*actor\.aiCoordinates.*mwmp::BaseActorList::ACTIVATE.*mwmp::BaseActorList::COMBAT.*mwmp::BaseActorList::FOLLOW.*getAiTargetPosition\(cell,\s*actor\.aiTarget,\s*destination\).*direction\s*=\s*zeroPosition\(\).*distanceSquared\s*<=\s*stopDistance\s*\*\s*stopDistance.*normalizeHorizontalIntent\(direction\.pos\[0\],\s*direction\.pos\[1\]\).*actor\.position\.rot\[2\]\s*=\s*std::atan2\(direction\.pos\[0\],\s*direction\.pos\[1\]\))(?=.*void\s+ServerSimulation::tickActors\(float\s+deltaSeconds\).*const\s+bool\s+serverOwnsActorMovement\s*=\s*hasServerOwnedActorMovement\(actor\).*buildAiMovementIntent\(\*cell,\s*actor,\s*direction\).*if\s*\(!hasServerMovementIntent\).*serverOwnsActorMovement.*direction\s*=\s*zeroPosition\(\).*sanitizeFinitePosition\(direction\).*if\s*\(!hasMovementIntent\(direction\)\).*\(hasServerMovementIntent\s*\|\|\s*serverOwnsActorMovement\)\s*&&\s*hasMovementIntent\(actor\.direction\).*actor\.direction\s*=\s*direction.*\+\+actor\.positionSequence.*simulateMovementPosition\(actor\.position,\s*actor\.position,\s*direction,\s*deltaSeconds\))(?=.*void\s+ActorFunctions::SendActorAI\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*serverCell->readActorList\(ID_ACTOR_AI,\s*&writeActorList\);)' `
    -Missing $missing

Test-Pattern -Name "Server-owned player cell changes carry AI followers across the accepted boundary" -Text $serverSimulation `
    -Pattern '(?=.*mPlayerAcceptedCells\[player\.guid\]\s*=\s*player\.cell)(?=.*void\s+ServerSimulation::removePlayer\(PacketGuid\s+guid\).*mPlayerAcceptedCells\.erase\(guid\))(?=.*bool\s+isPlayerFollowerPackage\(const\s+mwmp::BaseActor&\s+actor,\s*mwmp::PacketGuid\s+playerGuid\).*actor\.hasAiData.*actor\.hasAiTarget.*actor\.aiTarget\.isPlayer.*actor\.aiTarget\.guid\s*==\s*playerGuid.*mwmp::BaseActorList::FOLLOW.*mwmp::BaseActorList::ESCORT)(?=.*ESM::Position\s+makeFollowerCellChangePosition\(const\s+Player&\s+player,\s*std::size_t\s+followerIndex\).*forwardX\s*=\s*sinYaw.*rightX\s*=\s*cosYaw.*followerCellChangeBehindDistance.*followerCellChangeColumnSpacing.*position\.pos\[0\]\s*\+=\s*rightX\s*\*\s*lateral\s*-\s*forwardX\s*\*\s*behind.*position\.pos\[1\]\s*\+=\s*rightY\s*\*\s*lateral\s*-\s*forwardY\s*\*\s*behind)(?=.*class\s+ScopedReceivedActorList.*getReceivedActorList\(\).*mPreviousActorList\(\*mReceivedActorList\).*~ScopedReceivedActorList\(\).*mPreviousActorList)(?=.*persistServerGeneratedActorCellChange\(Player&\s+player,\s*mwmp::BaseActorList&\s+actorList\).*ScopedReceivedActorList\s+receivedActorList\(actorList\).*Script::Call<Script::CallbackIdentity\("OnActorCellChange"\)>)(?=.*moveFollowingActorsAcrossPlayerCellChange\(Player&\s+player,\s*const\s+ESM::Cell&\s+sourceCellData\).*isSameSimulationCell\(sourceCellData,\s*player\.cell\).*sourceCell->getActorList\(\).*movedFollowers\.guid\s*=\s*player\.guid.*isClientActorControlUpdateAllowed\(&actor\).*isPlayerFollowerPackage\(actor,\s*player\.guid\).*movedActor\.cell\s*=\s*player\.cell.*movedActor\.position\s*=\s*makeFollowerCellChangePosition\(player,\s*followerIndex\).*movedActor\.isFollowerCellChange\s*=\s*true.*movedActor\.hasPositionData\s*=\s*true.*persistServerGeneratedActorCellChange\(player,\s*movedFollowers\).*ActorProcessor::cacheCellChange\(movedFollowers\).*ActorProcessor::sendCellChangeToLoaded\(\*actorPacket,\s*movedFollowers\).*actorPacket->Send\(player\.guid\))(?=.*ServerSimulation::acceptPlayerCellChange\(Player&\s+player\).*previousCellIt\s*=\s*mPlayerAcceptedCells\.find\(player\.guid\).*previousAcceptedCell\s*=\s*previousCellIt->second.*mPlayerAcceptedCells\[player\.guid\]\s*=\s*player\.cell.*moveFollowingActorsAcrossPlayerCellChange\(player,\s*previousAcceptedCell\))' `
    -Missing $missing

Test-Pattern -Name "Server actor movement batches forward newest movement and anim flag sequences per actor" -Text ($actorSequenceCoalescing + "`n" + $serverPositionProcessor + "`n" + $serverSimulation + "`n" + $serverAnimFlagsProcessor) `
    -Pattern 'using\s+ActorIdentityKey\s*=\s*std::pair<unsigned\s+int,\s*unsigned\s+int>;.*acceptNewestPositionActor\(std::vector<BaseActor>&\s+acceptedActors,\s*std::map<ActorIdentityKey,\s*std::size_t>&\s+acceptedActorIndexes,\s*const\s+BaseActor&\s+actor\).*if\s*\(isNewerPositionSequence\(actor\.positionSequence,\s*acceptedActor\.positionSequence\)\)\s*acceptedActor\s*=\s*actor;.*acceptNewestAnimFlagsActor\(std::vector<BaseActor>&\s+acceptedActors,\s*std::map<ActorIdentityKey,\s*std::size_t>&\s+acceptedActorIndexes,\s*const\s+BaseActor&\s+actor\).*mergeNewestActorAnimFlags\(acceptedActor,\s*actor\);.*filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*std::map<ActorIdentityKey,\s*std::size_t>\s+acceptedActorIndexes;.*hasNewerPosition.*hasNewerAnimFlags.*acceptNewestAnimFlagsActor\(acceptedActors,\s*acceptedActorIndexes,\s*actor\);.*ProcessorActorPosition.*acceptNewestPositionActor\(acceptedActors,\s*acceptedActorIndexes,\s*actor\);.*ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList\)' `
    -Missing $missing

Test-Pattern -Name "Server actor event packets normalize stale or missing movement snapshots before forwarding" -Text ($actorSequenceCoalescing + "`n" + $serverSimulation + "`n" + $serverAnimPlayProcessor + "`n" + $serverAttackProcessor + "`n" + $serverCastProcessor + "`n" + $serverDeathProcessor) `
    -Pattern '(?=.*normalizeActorMovementSnapshot\(Cell\*\s+serverCell,\s*BaseActor&\s+actor\).*serverCell\s*==\s*nullptr\s*\|\|\s*!serverCell->containsActor\(actor\.refNum,\s*actor\.mpNum\).*BaseActor\*\s+currentActor\s*=\s*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\);.*actor\.hasPositionData.*isNewerPositionSequence\(actor\.positionSequence,\s*currentActor->positionSequence\).*currentActor->positionSequence\s*=\s*actor\.positionSequence;.*else\s+if\s*\(currentActor->hasPositionData\).*actor\.hasPositionData\s*=\s*true;.*actor\.positionSequence\s*=\s*currentActor->positionSequence;)(?=.*filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\).*normalizeActorMovementSnapshot\(serverCell,\s*actor\);)(?=.*ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*normalizeActorMovementSnapshot\(&serverCell,\s*actor\);)(?=.*ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*normalizeActorMovementSnapshot\(&serverCell,\s*actor\);)(?=.*filterActorDeathToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*normalizeActorMovementSnapshot\(serverCell,\s*actor\);)(?=.*ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\).*serverCell->sendToLoaded\(&packet,\s*&actorList\);)(?=.*ProcessorActorAttack.*acceptActorAttacks\(actorList,\s*\*serverCell\).*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);)(?=.*ProcessorActorCast.*acceptActorCasts\(actorList,\s*\*serverCell\).*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);)(?=.*ProcessorActorDeath.*filterActorDeathToServerAccepted\(serverCell,\s*actorList\).*OnActorDeath.*serverCell->sendToLoaded\(&packet,\s*&actorList\);)' `
    -Missing $missing

Test-Pattern -Name "Server drops movement and AI control from server-dead actors" -Text ($actorStatsAuthority + "`n" + $actorSequenceCoalescing + "`n" + $serverPositionProcessor + "`n" + $serverSimulation + "`n" + $serverAnimFlagsProcessor + "`n" + $serverAnimPlayProcessor + "`n" + $serverAttackProcessor + "`n" + $serverCastProcessor + "`n" + $serverAiProcessor + "`n" + $serverCellChangeProcessor + "`n" + $basePacketTest) `
    -Pattern '(?=.*hasServerAcceptedDeadActorState\(const\s+BaseActor&\s+storedActor\).*storedActor\.creatureStats\.mDead.*storedHealth\s*<=\s*healthDeadEpsilon)(?=.*isClientActorControlUpdateAllowed\(const\s+BaseActor\*\s+storedActor\).*hasServerAcceptedDeadActorState\(\*storedActor\))(?=.*filterActorListToKnownLiveActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorAiSnapshot\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorAttacks\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ServerSimulation::acceptActorCasts\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\))(?=.*ProcessorActorAttack.*acceptActorAttacks\(actorList,\s*\*serverCell\))(?=.*ProcessorActorCast.*acceptActorCasts\(actorList,\s*\*serverCell\))(?=.*ProcessorActorAI.*acceptActorAiSnapshot\(actorList,\s*\*serverCell\))(?=.*ProcessorActorCellChange.*isClientActorControlUpdateAllowed\(currentActor\))(?=.*clientActorControlAuthorityRejectsServerDeadActors)' `
    -Missing $missing

Test-Pattern -Name "Server drops actor equipment speech spell and test state from server-dead actors" -Text ($actorSequenceCoalescing + "`n" + $serverEquipmentProcessor + "`n" + $serverSpeechProcessor + "`n" + $serverSpellsActiveProcessor + "`n" + $serverActorTestProcessor) `
    -Pattern 'filterActorListToKnownLiveActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*isClientActorControlUpdateAllowed\(currentActor\).*filterActorEquipmentToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*isClientActorControlUpdateAllowed\(currentActor\).*ProcessorActorEquipment.*filterActorEquipmentToServerAccepted\(serverCell,\s*actorList\).*OnActorEquipment.*ProcessorActorSpeech.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*serverCell->sendToLoaded\(&packet,\s*&actorList\);.*ProcessorActorSpellsActive.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*OnActorSpellsActive.*ProcessorActorTest.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*OnActorTest' `
    -Missing $missing

Test-Pattern -Name "Server actor combat packets also fan out directly to targeted players" -Text ($serverCppCell + "`n" + $serverAttackProcessor + "`n" + $serverCastProcessor) `
    -Pattern 'void\s+Cell::sendToLoadedAndGuids\(mwmp::ActorPacket\s+\*actorPacket,\s*mwmp::BaseActorList\s+\*baseActorList,\s*const\s+std::vector<mwmp::PacketGuid>&\s+targetGuids\).*for\s*\(auto\s+pl\s*:\s*players\).*pl\s*!=\s*nullptr\s*&&\s*!pl->npc\.mName\.empty\(\).*for\s*\(const\s+mwmp::PacketGuid&\s+targetGuid\s*:\s*targetGuids\).*targetGuid\s*==\s*mwmp::unassignedPacketGuid\(\)\s*\|\|\s*targetGuid\s*==\s*baseActorList->guid.*Player\*\s+target\s*=\s*Players::getPlayer\(targetGuid\);.*target\s*!=\s*nullptr\s*&&\s*!target->npc\.mName\.empty\(\).*plList\.push_back\(target\);.*plList\.sort\(\);.*plList\.unique\(\);.*if\s*\(pl->guid\s*==\s*baseActorList->guid\)\s*continue;.*actorPacket->setActorList\(baseActorList\);.*actorPacket->Send\(pl->guid\);.*ProcessorActorAttack.*std::vector<PacketGuid>\s+targetGuids;.*actor\.attack\.isHit\s*&&\s*actor\.attack\.target\.isPlayer.*targetGuids\.push_back\(actor\.attack\.target\.guid\);.*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);.*ProcessorActorCast.*std::vector<PacketGuid>\s+targetGuids;.*actor\.cast\.target\.isPlayer.*targetGuids\.push_back\(actor\.cast\.target\.guid\);.*serverCell->sendToLoadedAndGuids\(&packet,\s*&actorList,\s*targetGuids\);' `
    -Missing $missing

Test-Pattern -Name "Server actor movement cache and broadcasts reject non-finite snapshots" -Text ($actorSequenceCoalescing + "`n" + $serverPositionProcessor + "`n" + $serverSimulation + "`n" + $serverAnimFlagsProcessor + "`n" + $serverCellChangeProcessor + "`n" + $serverCppCell) `
    -Pattern '(?=.*isFiniteActorPosition\(const\s+ESM::Position&\s+position\).*std::isfinite\(position\.pos\[0\]\).*std::isfinite\(position\.rot\[2\]\))(?=.*isFiniteActorMovementSnapshot\(const\s+BaseActor&\s+actor\).*isFiniteActorPosition\(actor\.position\)\s*&&\s*isFiniteActorPosition\(actor\.direction\))(?=.*normalizeActorMovementSnapshot\(Cell\*\s+serverCell,\s*BaseActor&\s+actor\).*actor\.hasPositionData\s*&&\s*!isFiniteActorMovementSnapshot\(actor\).*actor\.hasPositionData\s*=\s*false;)(?=.*filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*actor\.hasPositionData\s*&&\s*!isFiniteActorMovementSnapshot\(actor\).*actor\.hasPositionData\s*=\s*false;)(?=.*ProcessorActorPosition.*if\s*\(!isFiniteActorMovementSnapshot\(actor\)\).*addPositionCorrection\(serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\)\);.*continue;)(?=.*ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorCellChange.*if\s*\(isFiniteActorMovementSnapshot\(actor\).*actor\.cell\.getDescription\(\)\s*!=\s*actorList\.cell\.getDescription\(\)\).*acceptedActors\.push_back\(actor\);)(?=.*case\s+ID_ACTOR_POSITION:.*if\s*\(!isFiniteActorMovementSnapshot\(newActor\)\)\s*break;)(?=.*case\s+ID_ACTOR_ANIM_FLAGS:.*actorToCache.*!isFiniteActorMovementSnapshot\(actorToCache\).*actorToCache\.hasPositionData\s*=\s*false;.*mergeNewestActorAnimFlags\(\*cellActor,\s*actorToCache\);)' `
    -Missing $missing

Test-Pattern -Name "Server-script actor position sends become authoritative cached state" -Text ($actorFunctions + "`n" + $serverCppCell) `
    -Pattern 'void\s+advanceActorPositionSequences\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*for\s*\(BaseActor&\s+actor\s*:\s*actorList\.baseActors\).*findStoredActor\(serverCell,\s*actor\).*actor\.positionSequence\s*=\s*storedActor->positionSequence\s*\+\s*1;.*else\s*\+\+actor\.positionSequence;.*void\s+ActorFunctions::SendActorPosition\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&writeActorList\.cell\);.*advanceActorPositionSequences\(serverCell,\s*writeActorList\);.*serverCell->readActorList\(ID_ACTOR_POSITION,\s*&writeActorList\);.*GetPacket\(ID_ACTOR_POSITION\);.*actorPacket->setActorList\(&writeActorList\);.*if\s*\(!skipAttachedPlayer\).*actorPacket->Send\(writeActorList\.guid\);.*if\s*\(sendToOtherVisitors\).*serverCell->sendToLoaded\(actorPacket,\s*&writeActorList\);.*case\s+ID_ACTOR_POSITION:.*cellActor->hasPositionData\s*=\s*true;.*cellActor->positionSequence\s*=\s*newActor\.positionSequence;' `
    -Missing $missing

Test-Pattern -Name "Server-script actor lists normalize counts before cache reads and sends" -Text $actorFunctions `
    -Pattern 'void\s+syncActorListCount\(BaseActorList&\s+actorList\).*actorList\.count\s*=\s*static_cast<unsigned\s+int>\(actorList\.baseActors\.size\(\)\);.*void\s+ActorFunctions::ClearActorList\(\).*writeActorList\.baseActors\.clear\(\);.*syncActorListCount\(writeActorList\);.*void\s+ActorFunctions::CopyReceivedActorListToStore\(\).*readActorList\s*==\s*nullptr.*writeActorList\.baseActors\.clear\(\);.*syncActorListCount\(writeActorList\);.*writeActorList\s*=\s*\*readActorList;.*syncActorListCount\(writeActorList\);.*unsigned\s+int\s+ActorFunctions::GetActorListSize\(\).*return\s+static_cast<unsigned\s+int>\(readActorList->baseActors\.size\(\)\);.*void\s+ActorFunctions::AddActor\(\).*writeActorList\.baseActors\.push_back\(tempActor\);.*syncActorListCount\(writeActorList\);.*void\s+ActorFunctions::SendActorAI\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*syncActorListCount\(writeActorList\);.*serverCell->readActorList\(ID_ACTOR_AI,\s*&writeActorList\);' `
    -Missing $missing

Test-Pattern -Name "Server actor cell-change cache moves actors between loaded C++ cells" -Text ($serverCppCell + "`n" + $actorProcessorHeader + "`n" + $actorProcessor + "`n" + $serverCellChangeProcessor + "`n" + $actorFunctions) `
    -Pattern 'void\s+Cell::upsertActors\(const\s+mwmp::BaseActorList\s+\*newActorList\).*actorToStore\.cell\s*=\s*cell;.*getActor\(actorToStore\.refNum,\s*actorToStore\.mpNum\).*cellActor\s*=\s*actorToStore;.*cellActorList\.baseActors\.push_back\(actorToStore\);.*cellActorList\.count\s*=\s*static_cast<unsigned\s+int>\(cellActorList\.baseActors\.size\(\)\);.*static\s+void\s+cacheCellChange\(BaseActorList\s*&\s*actorList\);.*bool\s+isSameCell\(const\s+ESM::Cell&\s+left,\s*const\s+ESM::Cell&\s+right\).*BaseActor\s+buildMovedActor\(Cell\s+\*sourceCell,\s*const\s+BaseActor&\s+actor\).*movedActor\s*=\s*\*storedActor;.*movedActor\.cell\s*=\s*actor\.cell;.*movedActor\.hasPositionData\s*=\s*true;.*void\s+ActorProcessor::cacheCellChange\(BaseActorList\s*&\s*actorList\).*actorsToRemove\.baseActors\.clear\(\);.*isSameCell\(actorList\.cell,\s*actor\.cell\)\s*\|\|\s*!isFiniteActorMovementSnapshot\(actor\).*destinationCellData\s*=\s*actor\.cell;.*destinationCell->upsertActors\(&destinationActorList\);.*sourceCell->removeActors\(&actorsToRemove\);.*ProcessorActorCellChange.*cacheCellChange\(actorList\);.*void\s+ActorFunctions::SendActorCellChange\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*ActorProcessor::cacheCellChange\(writeActorList\);' `
    -Missing $missing

Test-Pattern -Name "Server accepts actor cell changes only for adjacent exterior authority moves, followers, or script moves" -Text $serverCellChangeProcessor `
    -Pattern 'isAdjacentExteriorCellChange\(const\s+ESM::Cell&\s+sourceCell,\s*const\s+ESM::Cell&\s+destinationCell\).*sourceCell\.isExterior\(\)\s*&&\s*destinationCell\.isExterior\(\).*std::abs\(sourceCell\.mData\.mX\s*-\s*destinationCell\.mData\.mX\)\s*<=\s*1.*std::abs\(sourceCell\.mData\.mY\s*-\s*destinationCell\.mData\.mY\)\s*<=\s*1.*BPP_INIT\(ID_ACTOR_CELL_CHANGE\).*serverCell\s*!=\s*nullptr.*std::vector<BaseActor>\s+liveActors;.*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorControlUpdateAllowed\(currentActor\).*actorList\.baseActors\s*=\s*liveActors;.*const\s+bool\s+hasCellAuthority\s*=\s*serverCell->hasAuthority\(actorList\.guid\);.*std::vector<BaseActor>\s+permittedActors;.*const\s+bool\s+canCrossCell\s*=\s*actor\.isFollowerCellChange\s*\|\|.*hasCellAuthority\s*&&\s*isAdjacentExteriorCellChange\(actorList\.cell,\s*actor\.cell\).*permittedActors\.push_back\(actor\);.*actorList\.baseActors\s*=\s*permittedActors;.*actorList\.count\s*=\s*static_cast<unsigned\s+int>\(actorList\.baseActors\.size\(\)\);.*if\s*\(actorList\.count\s*!=\s*0\).*cacheCellChange\(actorList\);.*else\s*\{.*cacheCellChange\(actorList\);.*isAccepted\s*=\s*true;.*Script::Call<Script::CallbackIdentity\("OnActorCellChange"\)>.*sendCellChangeToLoaded\(packet,\s*actorList\);' `
    -Missing $missing

Test-Pattern -Name "Server routes ActorAI through Lua only after loaded-cell simulation acceptance" -Text ($serverAiProcessor + "`n" + $serverSimulation + "`n" + $serverCppCell) `
    -Pattern '(?=.*#include\s+"apps/openmw-mp/ServerSimulation\.hpp")(?=.*BPP_INIT\(ID_ACTOR_AI\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*if\s*\(serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasPlayer\(&player\)\).*acceptActorAiSnapshot\(actorList,\s*\*serverCell\).*Script::Call<Script::CallbackIdentity\("OnActorAI"\)>\(player\.getId\(\),\s*actorList\.cell\.getDescription\(\)\.c_str\(\)\);)(?=.*bool\s+ServerSimulation::acceptActorAiSnapshot\(BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*hasValidActorAiSnapshot\(serverCell,\s*actor\).*buildServerAcceptedAiActor\(serverCell,\s*actor,\s*\*currentActor\).*serverCell\.readActorList\(ID_ACTOR_AI,\s*&actorList\);)(?=.*bool\s+Cell::hasPlayer\(const\s+Player\*\s+player\)\s+const.*std::find\(players\.begin\(\),\s*players\.end\(\),\s*player\))' `
    -Missing $missing

Test-Pattern -Name "Server retains authoritative ActorAI for visitor replay and authority handoff" -Text ($baseActor + "`n" + $serverCppCell + "`n" + $actorFunctions + "`n" + $eventHandler + "`n" + $serverCell + "`n" + $logicHandler) `
    -Pattern 'bool\s+hasAiData\s*=\s*false;.*packetID\s*==\s*ID_ACTOR_AI.*newActor\.hasAiData\s*=\s*true;.*case\s+ID_ACTOR_AI:.*cellActor->hasAiData\s*=\s*true;.*cellActor->hasAiTarget\s*=\s*newActor\.hasAiTarget;.*cellActor->aiTarget\s*=\s*newActor\.aiTarget;.*cellActor->aiAction\s*=\s*newActor\.aiAction;.*cellActor->aiDistance\s*=\s*newActor\.aiDistance;.*cellActor->aiDuration\s*=\s*newActor\.aiDuration;.*cellActor->aiShouldRepeat\s*=\s*newActor\.aiShouldRepeat;.*cellActor->aiCoordinates\s*=\s*newActor\.aiCoordinates;.*bool\s+ActorFunctions::DoesActorHaveAI\(unsigned\s+int\s+index\).*hasAiData;.*int\s+ActorFunctions::GetActorAITargetPid\(unsigned\s+int\s+index\).*Players::getPlayer\(readActorList->baseActors\.at\(index\)\.aiTarget\.guid\).*void\s+ActorFunctions::SendActorAI\(bool\s+sendToOtherVisitors,\s*bool\s+skipAttachedPlayer\).*serverCell->readActorList\(ID_ACTOR_AI,\s*&writeActorList\);.*eventHandler\.OnActorAI.*tes3mp\.SendActorAI\(true,\s*false\).*LoadedCells\[cellDescription\]:SaveActorAI\(\).*function\s+BaseCell:SaveActorAI\(\).*tes3mp\.ReadCellActorList\(self\.description\).*tes3mp\.DoesActorHaveAI\(objectIndex\).*action\s*==\s*enumerations\.ai\.CANCEL\s+or\s+action\s*==\s*enumerations\.ai\.ACTIVATE.*tableHelper\.removeValue\(self\.data\.packets\.ai,\s*uniqueIndex\).*dataTableBuilder\.BuildAIData\(targetPid,\s*targetUniqueIndex,.*tableHelper\.insertValueIfMissing\(self\.data\.packets\.ai,\s*uniqueIndex\).*SaveActorPositions\(\).*SaveActorStatsDynamic\(\).*SaveActorAI\(\)' `
    -Missing $missing

Test-Pattern -Name "Server registers actors through actor-list packets and drops unknown actor updates" -Text ($actorSequenceCoalescing + "`n" + $serverCppCell + "`n" + $actorFunctions + "`n" + $serverActorListProcessor + "`n" + $serverPositionProcessor + "`n" + $serverSimulation + "`n" + $serverAnimFlagsProcessor + "`n" + $serverAnimPlayProcessor + "`n" + $serverAttackProcessor + "`n" + $serverCastProcessor + "`n" + $serverDeathProcessor + "`n" + $serverAiProcessor + "`n" + $serverEquipmentProcessor + "`n" + $serverSpeechProcessor + "`n" + $serverSpellsActiveProcessor + "`n" + $serverStatsDynamicProcessor + "`n" + $serverActorTestProcessor) `
    -Pattern '(?=.*filterActorListToKnownActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*serverCell->containsActor\(actor\.refNum,\s*actor\.mpNum\))(?=.*filterActorListToKnownLiveActors\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+normalizeMovement\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*filterActorAnimFlagsToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*filterActorCombatToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList,\s*bool\s+requireMovement\).*isClientActorControlUpdateAllowed\(currentActor\))(?=.*filterActorDeathToServerAccepted\(Cell\*\s+serverCell,\s*BaseActorList&\s+actorList\).*serverCell->getActor\(actor\.refNum,\s*actor\.mpNum\))(?=.*case\s+ID_ACTOR_LIST:.*cellActor->refId\s*=\s*newActor\.refId;)(?=.*void\s+Cell::requestActorListFrom\(const\s+mwmp::PacketGuid&\s+guid\).*actorListRequestGuid\s*=\s*guid;)(?=.*void\s+ActorFunctions::SendActorList\(\).*writeActorList\.action\s*==\s*mwmp::BaseActorList::REQUEST.*serverCell->requestActorListFrom\(writeActorList\.guid\);)(?=.*ProcessorActorList.*serverCell->readActorList\(packetID,\s*&actorList\);)(?=.*ServerSimulation::acceptActorMovementSnapshot\(ActorPacket&\s+packet,\s*BaseActorList&\s+actorList,\s*Cell&\s+serverCell\).*BaseActor\*\s+currentActor\s*=\s*serverCell\.getActor\(actor\.refNum,\s*actor\.mpNum\);.*if\s*\(currentActor\s*==\s*nullptr\)\s*continue;)(?=.*ProcessorActorAnimFlags.*filterActorAnimFlagsToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorAnimPlay.*filterActorCombatToServerAccepted\(serverCell,\s*actorList,\s*false\))(?=.*ProcessorActorAttack.*acceptActorAttacks\(actorList,\s*\*serverCell\))(?=.*ProcessorActorCast.*acceptActorCasts\(actorList,\s*\*serverCell\))(?=.*ProcessorActorDeath.*filterActorDeathToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorAI.*acceptActorAiSnapshot\(actorList,\s*\*serverCell\))(?=.*ProcessorActorEquipment.*filterActorEquipmentToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorSpeech.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\))(?=.*ProcessorActorSpellsActive.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\))(?=.*ProcessorActorStatsDynamic.*filterActorStatsDynamicToServerAccepted\(serverCell,\s*actorList\))(?=.*ProcessorActorTest.*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\))' `
    -Missing $missing

Test-Pattern -Name "Server accepts generic actor list, equipment, and test packets only from current cell authority" -Text ($serverActorListProcessor + "`n" + $serverEquipmentProcessor + "`n" + $serverActorTestProcessor) `
    -Pattern '(?=.*ProcessorActorList.*BPP_INIT\(ID_ACTOR_LIST\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*==\s*nullptr\s*\|\|\s*!serverCell->hasAuthority\(actorList\.guid\).*actorList\.action\s*!=\s*mwmp::BaseActorList::SET.*consumePendingActorListRequestFrom\(actorList\.guid\).*serverCell->sendToLoaded\(&packet,\s*&actorList\).*OnActorList)(?=.*ProcessorActorEquipment.*BPP_INIT\(ID_ACTOR_EQUIPMENT\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasAuthority\(actorList\.guid\).*filterActorEquipmentToServerAccepted\(serverCell,\s*actorList\).*serverCell->readActorList\(packetID,\s*&actorList\).*OnActorEquipment.*serverCell->sendToLoaded\(&packet,\s*&actorList\))(?=.*ProcessorActorTest.*BPP_INIT\(ID_ACTOR_TEST\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasAuthority\(actorList\.guid\).*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*serverCell->sendToLoaded\(&packet,\s*&actorList\).*OnActorTest)' `
    -Missing $missing

Test-Pattern -Name "Server accepts actor speech, active spells, and dynamic stats only from current cell authority" -Text ($serverSpeechProcessor + "`n" + $serverSpellsActiveProcessor + "`n" + $serverStatsDynamicProcessor) `
    -Pattern '(?=.*ProcessorActorSpeech.*BPP_INIT\(ID_ACTOR_SPEECH\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasAuthority\(actorList\.guid\).*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*serverCell->sendToLoaded\(&packet,\s*&actorList\))(?=.*ProcessorActorSpellsActive.*BPP_INIT\(ID_ACTOR_SPELLS_ACTIVE\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasAuthority\(actorList\.guid\).*filterActorListToKnownLiveActors\(serverCell,\s*actorList,\s*false\).*OnActorSpellsActive.*serverCell->sendToLoaded\(&packet,\s*&actorList\))(?=.*ProcessorActorStatsDynamic.*BPP_INIT\(ID_ACTOR_STATS_DYNAMIC\).*Cell\s+\*serverCell\s*=\s*CellController::get\(\)->getCell\(&actorList\.cell\);.*serverCell\s*!=\s*nullptr\s*&&\s*serverCell->hasAuthority\(actorList\.guid\).*serverCell->readActorList\(packetID,\s*&actorList\).*OnActorStatsDynamic.*serverCell->sendToLoaded\(&packet,\s*&actorList\))' `
    -Missing $missing

Test-Pattern -Name "Server echoes normalized actor dynamic stats to the authority sender" -Text $serverStatsDynamicProcessor `
    -Pattern 'ProcessorActorStatsDynamic.*filterActorStatsDynamicToServerAccepted\(serverCell,\s*actorList\).*serverCell->readActorList\(packetID,\s*&actorList\).*serverCell->sendToLoaded\(&packet,\s*&actorList\).*packet\.setActorList\(&actorList\);.*packet\.Send\(actorList\.guid\);' `
    -Missing $missing

Test-Pattern -Name "Server Lua saves generic actor state only for logged-in players and loaded cells" -Text $eventHandler `
    -Pattern 'eventHandler\.OnGenericActorEvent\s*=\s*function\(pid,\s*cellDescription,\s*packetType\).*Players\[pid\]\s*~=\s*nil\s+and\s+Players\[pid\]:IsLoggedIn\(\).*local\s+canonicalCellDescription,\s*isCellLoaded\s*=\s*getLoadedCellDescription\(cellDescription\).*cellDescription\s*=\s*canonicalCellDescription.*if\s+isCellLoaded\s+then.*tes3mp\.ReadReceivedActorList\(\).*packetReader\.GetActorPacketTables\(packetType\)\.actors.*customEventHooks\.triggerValidators\("On"\s*\.\.\s*packetType,\s*\{pid,\s*cellDescription,\s*actors\}\).*LoadedCells\[cellDescription\]:SaveActorsByPacketType\(packetType,\s*actors,\s*pid\).*customEventHooks\.triggerHandlers\("On"\s*\.\.\s*packetType,\s*eventStatus,\s*\{pid,\s*cellDescription,\s*actors\}\).*else\s+tes3mp\.Kick\(pid\)' `
    -Missing $missing

Test-Pattern -Name "Server Lua actor-list snapshots are request-pid bound" -Text ($serverCell + "`n" + $eventHandler + "`n" + $serverLuaCompatTest) `
    -Pattern 'function\s+BaseCell:AddVisitor\(pid,\s*options\).*local\s+actorListRequestPid\s*=\s*pid.*self\.authority\s*~=\s*nil\s+and\s+tableHelper\.containsValue\(self\.visitors,\s*self\.authority\).*actorListRequestPid\s*=\s*self\.authority.*self:RequestActorList\(actorListRequestPid\).*function\s+BaseCell:SaveActorsByPacketType\(packetType,\s*actors,\s*pid\).*self:SaveActorList\(actors,\s*pid\).*function\s+BaseCell:SaveActorList\(actors,\s*pid\).*self\.isRequestingActorList\s*==\s*true\s+and\s+pid\s*~=\s*nil\s+and\s+self\.actorListRequestPid\s*~=\s*pid.*Rejected ActorList snapshot.*return.*self\.actorListRequestPid\s*=\s*nil.*LoadedCells\[cellDescription\]:SaveActorsByPacketType\(packetType,\s*actors,\s*pid\).*CellBaseRequestsActorListFromCurrentAuthority.*RequestActorList:1.*CellBaseActorListRequestRequiresRequestedPid.*cell:SaveActorList\(actors,\s*8\).*cell:SaveActorList\(actors,\s*7\)' `
    -Missing $missing

Test-Pattern -Name "Server Lua echoes accepted ActorAI to every visitor including the authority sender" -Text $eventHandler `
    -Pattern 'eventHandler\.OnActorAI\s*=\s*function\(pid,\s*cellDescription\).*Players\[pid\]\s*~=\s*nil\s+and\s+Players\[pid\]:IsLoggedIn\(\).*local\s+canonicalCellDescription,\s*isCellLoaded\s*=\s*getLoadedCellDescription\(cellDescription\).*cellDescription\s*=\s*canonicalCellDescription.*if\s+isCellLoaded\s+then.*customEventHooks\.triggerValidators\("OnActorAI",\s*\{pid,\s*cellDescription\}\).*tes3mp\.ReadReceivedActorList\(\).*tes3mp\.CopyReceivedActorListToStore\(\).*tes3mp\.SendActorAI\(true,\s*false\).*LoadedCells\[cellDescription\]:SaveActorAI\(\).*customEventHooks\.triggerHandlers\("OnActorAI",\s*eventStatus,\s*\{pid,\s*cellDescription\}\).*else\s+tes3mp\.Kick\(pid\)' `
    -Missing $missing

Test-Pattern -Name "Server Lua loads unloaded source cells temporarily before saving actor cell changes" -Text $eventHandler `
    -Pattern 'eventHandler\.OnActorCellChange\s*=\s*function\(pid,\s*cellDescription\).*local\s+canonicalCellDescription,\s*isCellLoaded\s*=\s*getLoadedCellDescription\(cellDescription\).*cellDescription\s*=\s*canonicalCellDescription.*if\s+not\s+isCellLoaded\s+then\s+logicHandler\.LoadCell\(cellDescription\).*customEventHooks\.triggerValidators\("OnActorCellChange",\s*\{pid,\s*cellDescription\}\).*LoadedCells\[cellDescription\]:SaveActorCellChanges\(pid\).*customEventHooks\.triggerHandlers\("OnActorCellChange",\s*eventStatus,\s*\{pid,\s*cellDescription\}\).*if\s+not\s+isCellLoaded\s+then\s+logicHandler\.UnloadCell\(cellDescription\)' `
    -Missing $missing

Test-Pattern -Name "Server Lua actor handlers resolve canonical exterior actor cells before using LoadedCells" -Text ($eventHandler + "`n" + $serverLuaCompatTest) `
    -Pattern 'local\s+function\s+getExteriorCellGrid\(cellDescription\).*string\.find\(cellDescription,\s*"\^\(-\?%d\+\),%s\*\(-\?%d\+\)\$"\).*local\s+function\s+getLoadedCellDescription\(cellDescription\).*if\s+LoadedCells\[cellDescription\]\s*~=\s*nil\s+then.*local\s+gridX,\s*gridY\s*=\s*getExteriorCellGrid\(cellDescription\).*for\s+loadedCellDescription,\s*_\s+in\s+pairs\(LoadedCells\)\s+do.*if\s+loadedGridX\s*==\s*gridX\s+and\s+loadedGridY\s*==\s*gridY\s+then.*eventHandler\.OnGenericActorEvent.*getLoadedCellDescription\(cellDescription\).*eventHandler\.OnActorStatsDynamic.*getLoadedCellDescription\(cellDescription\).*eventHandler\.OnActorAI.*getLoadedCellDescription\(cellDescription\).*eventHandler\.OnActorDeath.*getLoadedCellDescription\(cellDescription\).*eventHandler\.OnActorCellChange.*getLoadedCellDescription\(cellDescription\).*EventHandlerOnActorStatsDynamicAcceptsCanonicalExteriorActorCells.*eventHandler\.OnActorStatsDynamic\(4,\s*"-2, -9"\).*Wilderness \(-2, -9\)' `
    -Missing $missing

Test-Pattern -Name "Server cell persistence reloads stored actor positions and AI with target validation" -Text ($serverCell + "`n" + $packetBuilder) `
    -Pattern 'local\s+function\s+getLoggedInPlayerByStorageKey\(playerKey\).*type\(logicHandler\.GetLoggedInPlayerByStorageKey\)\s*==\s*"function".*function\s+BaseCell:LoadActorPositions\(pid,\s*objectData,\s*uniqueIndexArray\).*tes3mp\.SetActorPosition\(location\.posX,\s*location\.posY,\s*location\.posZ\).*tes3mp\.SetActorRotation\(location\.rotX,\s*location\.rotY,\s*location\.rotZ\).*tes3mp\.SendActorPosition\(\).*function\s+BaseCell:LoadActorAI\(pid,\s*objectData,\s*uniqueIndexArray\).*if\s+ai\.targetPlayer\s*~=\s*nil\s+or\s+ai\.targetPlayerKey\s*~=\s*nil\s+or\s+ai\.targetAccountName\s*~=\s*nil\s+then.*local\s+targetPlayer\s*=\s*getLoggedInPlayerByStorageKey\(ai\.targetPlayerKey\).*local\s+targetPlayerName\s*=\s*ai\.targetPlayer\s+or\s+ai\.targetAccountName.*if\s+targetPlayer\s*==\s*nil\s+and\s+targetPlayerName\s*~=\s*nil\s+then.*targetPlayer\s*=\s*logicHandler\.GetLoggedInPlayerByName\(targetPlayerName\).*ai\.action\s*==\s*enumerations\.ai\.ACTIVATE.*ai\.action\s*==\s*enumerations\.ai\.FOLLOW.*packetBuilder\.AddAIActor\(uniqueIndex,\s*targetPid,\s*ai\).*tes3mp\.SendActorAI\(false\).*sharedPacketUniqueIndexes.*packetBuilder\.AddAIActor\(uniqueIndex,\s*pid,\s*ai\).*tes3mp\.SendActorAI\(true\).*packetBuilder\.AddAIActor\s*=\s*function\(actorUniqueIndex,\s*targetPid,\s*aiData\).*tes3mp\.SetActorAIAction\(aiData\.action\).*tes3mp\.SetActorAITargetToPlayer\(targetPid\).*tes3mp\.SetActorAITargetToObject\(targetSplitIndex\[1\],\s*targetSplitIndex\[2\]\).*tes3mp\.SetActorAICoordinates\(aiData\.posX,\s*aiData\.posY,\s*aiData\.posZ\).*tes3mp\.SetActorAIRepetition\(aiData\.shouldRepeat\).*tes3mp\.AddActor\(\)' `
    -Missing $missing

Write-Host "TES3MP actor movement/AI sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 66"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
