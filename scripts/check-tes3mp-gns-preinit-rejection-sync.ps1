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

function Get-SourceTreeText {
    param([string[]]$RelativePaths)

    $texts = [System.Collections.Generic.List[string]]::new()
    foreach ($relativePath in $RelativePaths) {
        $path = Join-Path $SourceRoot $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Required source directory was not found: $path"
        }

        Get-ChildItem -LiteralPath $path -Recurse -File -Include *.hpp,*.cpp | ForEach-Object {
            $texts.Add((Get-Content -LiteralPath $_.FullName -Raw))
        }
    }

    return [string]::Join("`n", $texts)
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

function Test-PatternAbsent {
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

$serverNetworking = Get-SourceText "apps\openmw-mp\Networking.cpp"
$serverNetworkingHeader = Get-SourceText "apps\openmw-mp\Networking.hpp"
$serverPlayers = Get-SourceText "apps\openmw-mp\Player.cpp"
$serverPlayersHeader = Get-SourceText "apps\openmw-mp\Player.hpp"
$serverCell = Get-SourceText "apps\openmw-mp\Cell.cpp"
$serverCellHeader = Get-SourceText "apps\openmw-mp\Cell.hpp"
$serverScriptFunctions = Get-SourceText "apps\openmw-mp\Script\Functions\Server.cpp"
$serverScriptFunctionsHeader = Get-SourceText "apps\openmw-mp\Script\ScriptFunctions.hpp"
$serverScriptTypesHeader = Get-SourceText "apps\openmw-mp\Script\Types.hpp"
$serverConsoleInput = Get-SourceText "apps\openmw-mp\ConsoleInput.cpp"
$serverConsoleInputHeader = Get-SourceText "apps\openmw-mp\ConsoleInput.hpp"
$serverHandleInput = Get-SourceText "apps\openmw-mp\handleInput.cpp"
$serverMain = Get-SourceText "apps\openmw-mp\main.cpp"
$serverPlayerProcessor = Get-SourceText "apps\openmw-mp\processors\PlayerProcessor.cpp"
$serverPlayerProcessorHeader = Get-SourceText "apps\openmw-mp\processors\PlayerProcessor.hpp"
$serverActorProcessor = Get-SourceText "apps\openmw-mp\processors\ActorProcessor.cpp"
$serverActorProcessorHeader = Get-SourceText "apps\openmw-mp\processors\ActorProcessor.hpp"
$serverObjectProcessor = Get-SourceText "apps\openmw-mp\processors\ObjectProcessor.cpp"
$serverObjectProcessorHeader = Get-SourceText "apps\openmw-mp\processors\ObjectProcessor.hpp"
$serverWorldstateProcessor = Get-SourceText "apps\openmw-mp\processors\WorldstateProcessor.cpp"
$serverWorldstateProcessorHeader = Get-SourceText "apps\openmw-mp\processors\WorldstateProcessor.hpp"
$masterClient = Get-SourceText "apps\openmw-mp\MasterClient.cpp"
$masterClientHeader = Get-SourceText "apps\openmw-mp\MasterClient.hpp"
$clientNetworkingHeader = Get-SourceText "apps\openmw\mwmp\Networking.hpp"
$clientNetworking = Get-SourceText "apps\openmw\mwmp\Networking.cpp"
$clientBaseClientPacketProcessor = Get-SourceText "apps\openmw\mwmp\processors\BaseClientPacketProcessor.cpp"
$clientBaseClientPacketProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\BaseClientPacketProcessor.hpp"
$clientMechanicsHelper = Get-SourceText "apps\openmw\mwmp\MechanicsHelper.cpp"
$clientSystemProcessor = Get-SourceText "apps\openmw\mwmp\processors\SystemProcessor.cpp"
$clientSystemProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\SystemProcessor.hpp"
$clientPlayerProcessor = Get-SourceText "apps\openmw\mwmp\processors\PlayerProcessor.cpp"
$clientPlayerProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\PlayerProcessor.hpp"
$clientActorProcessor = Get-SourceText "apps\openmw\mwmp\processors\ActorProcessor.cpp"
$clientActorProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\ActorProcessor.hpp"
$clientObjectProcessor = Get-SourceText "apps\openmw\mwmp\processors\ObjectProcessor.cpp"
$clientObjectProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\ObjectProcessor.hpp"
$clientWorldstateProcessor = Get-SourceText "apps\openmw\mwmp\processors\WorldstateProcessor.cpp"
$clientWorldstateProcessorHeader = Get-SourceText "apps\openmw\mwmp\processors\WorldstateProcessor.hpp"
$clientPlayerList = Get-SourceText "apps\openmw\mwmp\PlayerList.cpp"
$clientPlayerListHeader = Get-SourceText "apps\openmw\mwmp\PlayerList.hpp"
$clientDedicatedPlayer = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.cpp"
$clientDedicatedPlayerHeader = Get-SourceText "apps\openmw\mwmp\DedicatedPlayer.hpp"
$clientWorldstate = Get-SourceText "apps\openmw\mwmp\Worldstate.cpp"
$clientCell = Get-SourceText "apps\openmw\mwmp\Cell.cpp"
$clientCellHeader = Get-SourceText "apps\openmw\mwmp\Cell.hpp"
$clientGuiController = Get-SourceText "apps\openmw\mwmp\GUIController.cpp"
$clientGuiControllerHeader = Get-SourceText "apps\openmw\mwmp\GUIController.hpp"
$clientProcessorPlayerAlly = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorPlayerAlly.hpp"
$clientProcessorUserDisconnected = Get-SourceText "apps\openmw\mwmp\processors\player\ProcessorUserDisconnected.hpp"
$clientBaseIdentityHeaders = [string]::Join("`n", @(
    (Get-SourceText "apps\openmw\mwmp\ActorList.hpp"),
    (Get-SourceText "apps\openmw\mwmp\ObjectList.hpp"),
    (Get-SourceText "apps\openmw\mwmp\LocalPlayer.hpp"),
    (Get-SourceText "apps\openmw\mwmp\LocalSystem.hpp")
))
$rootCMake = Get-SourceText "CMakeLists.txt"
$componentsCMake = Get-SourceText "components\CMakeLists.txt"
$openMwCMake = Get-SourceText "apps\openmw\CMakeLists.txt"
$serverCMake = Get-SourceText "apps\openmw-mp\CMakeLists.txt"
$browserCMake = Get-SourceText "apps\browser\CMakeLists.txt"
$masterCMake = Get-SourceText "apps\master\CMakeLists.txt"
$componentTestsCMake = Get-SourceText "apps\components_tests\CMakeLists.txt"
$updateOpenMw = Get-SourceText "scripts\update-openmw.ps1"
$windowsWorkflow = Get-SourceText ".github\workflows\windows.yml"
$pushWorkflow = Get-SourceText ".github\workflows\push.yml"
$openMwSyncWorkflow = Get-SourceText ".github\workflows\openmw-sync.yml"
$macosWorkflow = Get-SourceText ".github\workflows\macos.yml"
$macosScript = Get-SourceText "CI\before_script.macos.sh"
$networkMessagesHeader = Get-SourceText "components\openmw-mp\NetworkMessages.hpp"
$baseActorHeader = Get-SourceText "components\openmw-mp\Base\BaseActor.hpp"
$baseObjectHeader = Get-SourceText "components\openmw-mp\Base\BaseObject.hpp"
$basePlayerHeader = Get-SourceText "components\openmw-mp\Base\BasePlayer.hpp"
$baseStructsHeader = Get-SourceText "components\openmw-mp\Base\BaseStructs.hpp"
$baseSystemHeader = Get-SourceText "components\openmw-mp\Base\BaseSystem.hpp"
$baseWorldstateHeader = Get-SourceText "components\openmw-mp\Base\BaseWorldstate.hpp"
$baseIdentityHeaders = [string]::Join("`n", @(
    $baseActorHeader,
    $baseObjectHeader,
    $basePlayerHeader,
    $baseStructsHeader,
    $baseSystemHeader,
    $baseWorldstateHeader
))
$basePacketHeader = Get-SourceText "components\openmw-mp\Packets\BasePacket.hpp"
$basePacket = Get-SourceText "components\openmw-mp\Packets\BasePacket.cpp"
$actorPacket = Get-SourceText "components\openmw-mp\Packets\Actor\ActorPacket.cpp"
$objectPacket = Get-SourceText "components\openmw-mp\Packets\Object\ObjectPacket.cpp"
$playerPacket = Get-SourceText "components\openmw-mp\Packets\Player\PlayerPacket.cpp"
$systemPacket = Get-SourceText "components\openmw-mp\Packets\System\SystemPacket.cpp"
$worldstatePacket = Get-SourceText "components\openmw-mp\Packets\Worldstate\WorldstatePacket.cpp"
$systemPacketControllerHeader = Get-SourceText "components\openmw-mp\Controllers\SystemPacketController.hpp"
$playerPacketControllerHeader = Get-SourceText "components\openmw-mp\Controllers\PlayerPacketController.hpp"
$actorPacketControllerHeader = Get-SourceText "components\openmw-mp\Controllers\ActorPacketController.hpp"
$objectPacketControllerHeader = Get-SourceText "components\openmw-mp\Controllers\ObjectPacketController.hpp"
$worldstatePacketControllerHeader = Get-SourceText "components\openmw-mp\Controllers\WorldstatePacketController.hpp"
$systemPacketController = Get-SourceText "components\openmw-mp\Controllers\SystemPacketController.cpp"
$playerPacketController = Get-SourceText "components\openmw-mp\Controllers\PlayerPacketController.cpp"
$actorPacketController = Get-SourceText "components\openmw-mp\Controllers\ActorPacketController.cpp"
$objectPacketController = Get-SourceText "components\openmw-mp\Controllers\ObjectPacketController.cpp"
$worldstatePacketController = Get-SourceText "components\openmw-mp\Controllers\WorldstatePacketController.cpp"
$packetPreInit = Get-SourceText "components\openmw-mp\Packets\PacketPreInit.cpp"
$basePacketTests = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"
$packetDeliveryHeader = Get-SourceText "components\openmw-mp\Transport\PacketDelivery.hpp"
$packetDestinationHeader = Get-SourceText "components\openmw-mp\Transport\PacketDestination.hpp"
$packetIdHeader = Get-SourceText "components\openmw-mp\Transport\PacketId.hpp"
$packetIdentityHeader = Get-SourceText "components\openmw-mp\Transport\PacketIdentity.hpp"
$packetStreamHeader = Get-SourceText "components\openmw-mp\Transport\PacketStream.hpp"
$packetStreamSource = Get-SourceText "components\openmw-mp\Transport\PacketStream.cpp"
$packetTransportHeader = Get-SourceText "components\openmw-mp\Transport\PacketTransport.hpp"
$receivedPacketHeader = Get-SourceText "components\openmw-mp\Transport\ReceivedPacket.hpp"
$packetAndMasterTree = Get-SourceTreeText @("components\openmw-mp\Packets", "components\openmw-mp\Master")
$gnsTransportHeader = Get-SourceText "components\openmw-mp\Transport\GnsTransport.hpp"
$gnsTransport = Get-SourceText "components\openmw-mp\Transport\GnsTransport.cpp"
$gnsTransportTests = Get-SourceText "apps\components_tests\openmw-mp\gnstransport.cpp"
$loginOrderingGuard = Get-SourceText "scripts\check-tes3mp-login-world-entry-ordering.ps1"
$luaCompatWrapper = Get-SourceText "scripts\test-tes3mp-lua-compat.ps1"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "TES3MP builds require GNS in CMake" -Text $rootCMake `
    -Pattern 'set\(BUILD_TES3MP_TARGETS\s+OFF\).*if\s*\(BUILD_TES3MP_CLIENT\s+OR\s+BUILD_TES3MP_SERVER\s+OR\s+BUILD_TES3MP_MASTER\).*set\(BUILD_TES3MP_TARGETS\s+ON\).*if\s*\(BUILD_TES3MP_TARGETS\).*find_package\(GameNetworkingSockets\s+CONFIG\s+REQUIRED\)' `
    -Missing $missing

Test-PatternAbsent -Name "Build configuration exposes no TES3MP_USE_GNS compatibility switch" -Text ($rootCMake + "`n" + $updateOpenMw + "`n" + $windowsWorkflow + "`n" + $pushWorkflow + "`n" + $macosScript) `
    -Pattern 'TES3MP_USE_GNS' `
    -Missing $missing

Test-Pattern -Name "TES3MP local targets wire GNS from mandatory build targets, not legacy transport switches" -Text ($componentsCMake + "`n" + $openMwCMake + "`n" + $serverCMake + "`n" + $componentTestsCMake) `
    -Pattern 'if\s*\(BUILD_TES3MP_TARGETS\).*add_component_dir\s*\(openmw-mp/Transport\s+GnsTransport\s+\).*if\s*\(BUILD_TES3MP_TARGETS\).*target_link_libraries\(components\s+GameNetworkingSockets::GameNetworkingSockets\).*if\s*\(BUILD_TES3MP_CLIENT\).*target_link_libraries\(openmw-lib\s+Boost::filesystem\s+GameNetworkingSockets::GameNetworkingSockets\).*target_link_libraries\(tes3mp-server.*GameNetworkingSockets::GameNetworkingSockets.*if\s*\(BUILD_TES3MP_TARGETS\).*openmw-mp/gnstransport\.cpp' `
    -Missing $missing

Test-PatternAbsent -Name "Local TES3MP target wiring has no TES3MP_USE_GNS branch" -Text ($componentsCMake + "`n" + $openMwCMake + "`n" + $serverCMake + "`n" + $componentTestsCMake) `
    -Pattern 'if\s*\(\s*TES3MP_USE_GNS\s*\)' `
    -Missing $missing

Test-PatternAbsent -Name "TES3MP target wiring does not require or link RakNet" -Text ($rootCMake + "`n" + $componentsCMake + "`n" + $openMwCMake + "`n" + $serverCMake + "`n" + $browserCMake + "`n" + $masterCMake) `
    -Pattern 'find_package\(RakNet|RakNet_INCLUDES|RakNet_LIBRARY|RakNet_ROOT' `
    -Missing $missing

Test-Pattern -Name "BasePacket sends only through PacketTransport" -Text $basePacket `
    -Pattern 'uint32_t\s+BasePacket::RequestData\(PacketGuid\s+targetGuid\).*if\s*\(!sTransport\)\s*return\s+0;.*return\s+sTransport->send\(bsSend->data\(\),\s*bsSend->size\(\),\s*PacketPriority::High,\s*PacketReliability::ReliableOrdered,\s*orderChannel,\s*PacketDestination\(targetGuid\),\s*false\);.*uint32_t\s+BasePacket::Send\(const\s+PacketDestination&\s+destination\).*if\s*\(!sTransport\)\s*return\s+0;.*return\s+sTransport->send\(bsSend->data\(\),\s*bsSend->size\(\),\s*priority,\s*reliability,\s*orderChannel,\s*destination,\s*false\);.*uint32_t\s+BasePacket::Send\(bool\s+toOther\).*if\s*\(!sTransport\)\s*return\s+0;.*return\s+sTransport->send\(bsSend->data\(\),\s*bsSend->size\(\),\s*priority,\s*reliability,\s*orderChannel,\s*PacketDestination\(guid\),\s*toOther\);' `
    -Missing $missing

Test-PatternAbsent -Name "Packet hierarchy has no RakPeer send fallback or stored peer" -Text ($basePacketHeader + "`n" + $basePacket + "`n" + $actorPacket + "`n" + $objectPacket + "`n" + $playerPacket + "`n" + $systemPacket + "`n" + $worldstatePacket) `
    -Pattern 'RakPeerInterface|peer->Send|BasePacket\(peer\)|this->peer|#include\s+<RakPeer\.h>' `
    -Missing $missing

Test-Pattern -Name "BasePacket strings use TES3MP-owned length-prefixed serialization" -Text ($basePacketHeader + "`n" + $basePacketTests) `
    -Pattern 'bool\s+RW\(std::string\s+&str,\s*bool\s+write,\s*bool\s+compress\s*=\s*false,\s*std::string::size_type\s+maxSize\s*=\s*maxStrSize\).*const\s+uint32_t\s+serializedSize\s*=\s*static_cast<uint32_t>\(writeSize\);.*bs->Write\(serializedSize\);.*bs->Write\(str\.data\(\),\s*serializedSize\);.*uint32_t\s+serializedSize\s*=\s*0;.*bs->Read\(serializedSize\).*serializedSize\s*>\s*maxStrSize.*bs->Read\(value\.data\(\),\s*serializedSize\).*value\.resize\(std::min<std::string::size_type>\(value\.size\(\),\s*maxSize\)\);.*TEST\(MpBasePacketTest,\s*stringsUseTes3mpLengthPrefixedSerializationAndClampToMaxSize\).*EXPECT_EQ\(serializedSize,\s*4u\);.*EXPECT_EQ\(received,\s*"abc"\);' `
    -Missing $missing

Test-Pattern -Name "BasePacket read helpers poison invalid packets on failed reads" -Text ($basePacketHeader + "`n" + $basePacketTests) `
    -Pattern '(?=.*bool\s+RW\(templateType\s+&data,\s*uint32_t\s+size,\s*bool\s+write\).*bs->Read\(data,\s*size\).*packetValid\s*=\s*false)(?=.*bool\s+RW\(templateType\s+&data,\s*bool\s+write,\s*bool\s+compress\s*=\s*0\).*ReadCompressed\(data\).*packetValid\s*=\s*false)(?=.*bool\s+RW\(bool\s+&data,\s*bool\s+write\).*bs->Read\(data\).*packetValid\s*=\s*false)(?=.*bool\s+RW\(std::string\s+&str,\s*bool\s+write.*bs->Read\(serializedSize\).*packetValid\s*=\s*false.*serializedSize\s*>\s*maxStrSize.*packetValid\s*=\s*false.*bs->Read\(value\.data\(\),\s*serializedSize\).*packetValid\s*=\s*false)(?=.*type\s*!=\s*ArgType::RefId\s*&&\s*type\s*!=\s*ArgType::RefNum.*packetValid\s*=\s*false)(?=.*failedPrimitiveAndStringReadsMarkPacketInvalid)(?=.*invalidVariantTagMarksPacketInvalid)(?=.*truncatedPlayerPacketPayloadMarksPacketInvalid)' `
    -Missing $missing

Test-PatternAbsent -Name "BasePacket string serialization has no direct RakString dependency" -Text ($basePacketHeader + "`n" + $masterClientHeader) `
    -Pattern 'RakString|SerializeCompressed|DeserializeCompressed|AppendBytes|Truncate\(' `
    -Missing $missing

Test-Pattern -Name "Packet delivery priority and reliability are TES3MP-owned transport enums" -Text ($componentsCMake + "`n" + $packetDeliveryHeader + "`n" + $packetTransportHeader + "`n" + $basePacketHeader + "`n" + $gnsTransport) `
    -Pattern 'add_component_dir\s*\(openmw-mp/Transport\s+PacketDelivery\s+PacketDestination\s+PacketId\s+PacketIdentity\s+PacketStream\s+PacketTransport\s+ReceivedPacket\s+\).*enum\s+class\s+PacketPriority.*Immediate.*High.*Medium.*Low.*enum\s+class\s+PacketReliability.*Unreliable.*UnreliableSequenced.*UnreliableWithAckReceipt.*Reliable.*ReliableOrdered.*ReliableOrderedWithAckReceipt.*virtual\s+uint32_t\s+send\(const\s+unsigned\s+char\*\s+data,\s*std::size_t\s+length,\s*PacketPriority\s+priority,\s*PacketReliability\s+reliability.*PacketReliability::Unreliable.*PacketPriority::Immediate' `
    -Missing $missing

Test-Pattern -Name "Packet destination is a TES3MP-owned transport wrapper" -Text ($componentsCMake + "`n" + $packetIdentityHeader + "`n" + $packetDestinationHeader + "`n" + $packetTransportHeader + "`n" + $gnsTransportHeader + "`n" + $basePacketTests) `
    -Pattern '(?=.*add_component_dir\s*\(openmw-mp/Transport\s+PacketDelivery\s+PacketDestination\s+PacketId\s+PacketIdentity\s+PacketStream\s+PacketTransport\s+ReceivedPacket\s+\))(?=.*struct\s+PacketAddress.*std::string\s+host;.*unsigned\s+short\s+port\s*=\s*0;.*operator==\(const\s+PacketAddress&\s+left,\s*const\s+PacketAddress&\s+right\).*operator<\(const\s+PacketAddress&\s+left,\s*const\s+PacketAddress&\s+right\))(?=.*struct\s+PacketNetworkId.*std::uint64_t\s+value\s*=\s*0;)(?=.*struct\s+PacketGuid.*std::uint64_t\s+value\s*=\s*static_cast<std::uint64_t>\(-1\);.*operator==\(PacketGuid\s+left,\s*PacketGuid\s+right\).*operator<\(PacketGuid\s+left,\s*PacketGuid\s+right\))(?=.*PacketGuid\s+unassignedPacketGuid\(\).*return\s+PacketGuid\{\};)(?=.*PacketAddress\s+unassignedPacketAddress\(\).*return\s+PacketAddress\{\};)(?=.*PacketAddress\s+makePacketAddress\(const\s+char\*\s+host,\s*unsigned\s+short\s+port\).*return\s+PacketAddress\{\s*hostText,\s*port\s*\};)(?=.*bool\s+isPacketAddressNumericHost\(const\s+std::string&\s+host\).*std::all_of\(hostText\.begin\(\),\s*hostText\.end\(\))(?=.*unsigned\s+short\s+packetAddressPort\(const\s+PacketAddress&\s+address\).*return\s+address\.port;)(?=.*void\s+setPacketAddressPortHostOrder\(PacketAddress&\s+address,\s*unsigned\s+short\s+port\).*address\.port\s*=\s*port;)(?=.*std::size_t\s+packetGuidSize\(\).*return\s+sizeof\(std::uint64_t\);)(?=.*PacketGuid\s+makePacketGuid\(std::uint64_t\s+value\).*return\s+PacketGuid\{\s*value\s*\};)(?=.*std::uint64_t\s+packetGuidValue\(PacketGuid\s+guid\).*return\s+guid\.value;)(?=.*std::string\s+packetGuidToString\(PacketGuid\s+guid\).*UNASSIGNED_PACKET_GUID.*std::to_string\(guid\.value\))(?=.*writePacketAddress\(Stream&\s+stream,\s*const\s+PacketAddress&\s+address\).*stream\.Write\(hostSize\).*stream\.Write\(address\.host\.data\(\),\s*hostSize\).*stream\.Write\(address\.port\))(?=.*readPacketAddress\(Stream&\s+stream,\s*PacketAddress&\s+address\).*stream\.Read\(hostSize\).*stream\.Read\(host\.data\(\),\s*hostSize\).*stream\.Read\(port\).*address\s*=\s*makePacketAddress\(host\.c_str\(\),\s*port\))(?=.*bool\s+isPacketGuidAssigned\(PacketGuid\s+guid\))(?=.*bool\s+isPacketAddressAssigned\(const\s+PacketAddress&\s+address\).*return\s+!address\.host\.empty\(\);)(?=.*class\s+PacketDestination.*PacketDestination\(const\s+PacketAddress&\s+address\).*PacketDestination\(PacketGuid\s+guid\).*PacketDestination\(PacketGuid\s+guid,\s*const\s+PacketAddress&\s+address\).*PacketGuid\s+guid\(\)\s+const.*const\s+PacketAddress&\s+address\(\)\s+const.*bool\s+hasGuid\(\)\s+const\s*\{\s*return\s+isPacketGuidAssigned\(mGuid\);.*PacketGuid\s+mGuid\s*=\s*unassignedPacketGuid\(\);)(?=.*virtual\s+uint32_t\s+send\(const\s+unsigned\s+char\*\s+data,\s*std::size_t\s+length,\s*PacketPriority\s+priority,\s*PacketReliability\s+reliability,\s*int8_t\s+orderChannel,\s*const\s+PacketDestination&\s+destination,\s*bool\s+broadcast\))(?=.*PacketAddress\s+getPacketAddress\(PacketGuid\s+guid\)\s+const\s+override;)(?=.*PacketGuid\s+getMyGuid\(\)\s+const\s+override;)(?=.*EXPECT_EQ\(transport\.sentDestination\.guid\(\),\s*testGuid\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "Packet identity helper coverage pins address formatting and serialization" -Text $basePacketTests `
    -Pattern 'TEST\(MpPacketIdentityTest,\s*packetAddressHelpersFormatAndRoundTripWithoutRakNetTypes\).*makePacketAddress\("\[::1\]",\s*25565\).*EXPECT_EQ\(packetAddressToString\(loopback,\s*true\),\s*"\[::1\]:25565"\).*EXPECT_EQ\(packetAddressToString\(loopback,\s*true,\s*''\|''\),\s*"::1\|25565"\).*setPacketAddressPortHostOrder\(hostname,\s*25566\).*ASSERT_TRUE\(writePacketAddress\(stream,\s*loopback\)\).*ASSERT_TRUE\(readPacketAddress\(stream,\s*received\)\).*EXPECT_EQ\(received,\s*loopback\).*EXPECT_FALSE\(writePacketAddress\(oversizedStream,\s*tooLong\)\).*EXPECT_FALSE\(readPacketAddress\(truncatedStream,\s*unchanged\)\);' `
    -Missing $missing

Test-Pattern -Name "Packet GUID header serialization is routed through PacketIdentity helpers" -Text ($packetIdentityHeader + "`n" + $basePacket + "`n" + $clientSystemProcessor + "`n" + $clientPlayerProcessor + "`n" + $clientActorProcessor + "`n" + $clientObjectProcessor + "`n" + $clientWorldstateProcessor + "`n" + $gnsTransportTests) `
    -Pattern '(?=.*template\s*<class\s+Stream>\s*inline\s+void\s+writePacketGuid\(Stream&\s+stream,\s*PacketGuid\s+guid\).*stream\.Write\(packetGuidValue\(guid\)\);)(?=.*template\s*<class\s+Stream>\s*inline\s+bool\s+readPacketGuid\(Stream&\s+stream,\s*PacketGuid&\s+guid\).*std::uint64_t\s+value\s*=\s*0;.*stream\.Read\(value\).*guid\s*=\s*makePacketGuid\(value\);)(?=.*writePacketGuid\(\*bs,\s*guid\);)(?=.*writePacketGuid\(\*bsSend,\s*targetGuid\);)(?=.*writePacketGuid\(stream,\s*guid\);)(?=.*readPacketGuid\(bsIn,\s*guid\))(?=.*writePacketGuid\(requestStream,\s*client\.getMyGuid\(\)\);)(?=.*readPacketGuid\(clientRequestRead,\s*requestedGuid\))' `
    -Missing $missing

Test-PatternAbsent -Name "Packet GUID header serialization does not use direct PacketStream GUID overloads" -Text ($basePacket + "`n" + $clientSystemProcessor + "`n" + $clientPlayerProcessor + "`n" + $clientActorProcessor + "`n" + $clientObjectProcessor + "`n" + $clientWorldstateProcessor + "`n" + $gnsTransportTests) `
    -Pattern '(?:\.|->)(?:Read|Write)\(\s*(?:targetGuid|guid|requestedGuid|client\.getMyGuid\(\))\s*\)' `
    -Missing $missing

Test-Pattern -Name "PacketStream remains part of the TES3MP transport component set" -Text $componentsCMake `
    -Pattern 'add_component_dir\s*\(openmw-mp/Transport\s+PacketDelivery\s+PacketDestination\s+PacketId\s+PacketIdentity\s+PacketStream\s+PacketTransport\s+ReceivedPacket\s+\)' `
    -Missing $missing

Test-Pattern -Name "PacketStream public header owns wrapper API without exposing implementation" -Text $packetStreamHeader `
    -Pattern 'class\s+PacketStreamImpl;.*class\s+PacketStream.*PacketStream\(\);.*PacketStream\(unsigned\s+char\*\s+data,\s*unsigned\s+int\s+lengthInBytes\);.*~PacketStream\(\);.*PacketStream\(PacketStream&&\)\s+noexcept;.*PacketStream&\s+operator=\(PacketStream&&\)\s+noexcept;.*PacketStream\(const\s+PacketStream&\)\s*=\s*delete;.*template\s*<class\s+T>\s*void\s+Write\(const\s+T&\s+data\).*WriteRaw\(&data,\s*sizeof\(T\)\);.*void\s+Write\(bool\s+data\);.*template\s*<class\s+T>\s*bool\s+Read\(T&\s+data\).*ReadRaw\(&data,\s*sizeof\(T\)\);.*bool\s+Read\(bool&\s+data\);.*WriteCompressedRaw\(&data,\s*sizeof\(T\)\);.*ReadCompressedRaw\(&data,\s*sizeof\(T\)\);.*std::unique_ptr<PacketStreamImpl>\s+mImpl;' `
    -Missing $missing

Test-Pattern -Name "PacketStream private implementation owns TES3MP bitstream storage" -Text $packetStreamSource `
    -Pattern 'class\s+PacketStreamImpl.*std::vector<unsigned\s+char>\s+mData;.*std::size_t\s+mNumberOfBitsUsed\s*=\s*0;.*std::size_t\s+mReadOffset\s*=\s*0;' `
    -Missing $missing

Test-Pattern -Name "PacketStream private implementation owns lifetime and move operations" -Text $packetStreamSource `
    -Pattern 'PacketStream::PacketStream\(\).*mImpl\(std::make_unique<PacketStreamImpl>\(\)\).*PacketStream::PacketStream\(unsigned\s+char\*\s+data,\s*unsigned\s+int\s+lengthInBytes\).*PacketStreamImpl>\(data,\s*lengthInBytes\).*PacketStream::~PacketStream\(\)\s*=\s*default;.*PacketStream::PacketStream\(PacketStream&&\)\s+noexcept\s*=\s*default;.*PacketStream&\s+PacketStream::operator=\(PacketStream&&\)\s+noexcept\s*=\s*default;' `
    -Missing $missing

Test-Pattern -Name "PacketStream preserves legacy bool and compressed float/double behavior" -Text $packetStreamSource `
    -Pattern 'void\s+PacketStream::Write\(bool\s+data\).*mImpl->write\(data\);.*bool\s+PacketStream::Read\(bool&\s+data\).*mImpl->read\(data\);.*void\s+PacketStream::WriteCompressed\(bool\s+data\).*Write\(data\);.*void\s+PacketStream::WriteCompressed\(float\s+data\).*std::clamp\(data,\s*-1\.0f,\s*1\.0f\).*Write\(static_cast<std::uint16_t>\(\(clamped\s+\+\s+1\.0f\)\s+\*\s+32767\.5f\)\);.*void\s+PacketStream::WriteCompressed\(double\s+data\).*std::clamp\(data,\s*-1\.0,\s*1\.0\).*Write\(static_cast<std::uint32_t>\(\(clamped\s+\+\s+1\.0\)\s+\*\s+2147483648\.0\)\);.*bool\s+PacketStream::ReadCompressed\(bool&\s+data\).*return\s+Read\(data\);.*bool\s+PacketStream::ReadCompressed\(float&\s+data\).*std::uint16_t\s+compressed\s*=\s*0;.*data\s*=\s*static_cast<float>\(compressed\)\s*/\s*32767\.5f\s*-\s*1\.0f;.*bool\s+PacketStream::ReadCompressed\(double&\s+data\).*std::uint32_t\s+compressed\s*=\s*0;.*data\s*=\s*static_cast<double>\(compressed\)\s*/\s*2147483648\.0\s*-\s*1\.0;' `
    -Missing $missing

Test-Pattern -Name "PacketStream raw helpers keep legacy byte serialization TES3MP-owned" -Text $packetStreamSource `
    -Pattern 'writeCompressedBytes\(PacketStreamImpl&\s+stream,\s*const\s+unsigned\s+char\*\s+data,\s*unsigned\s+int\s+sizeInBits\).*stream\.writeBits\(data,\s*\(currentByte\s+\+\s+1\)\s*<<\s*3,\s*true\).*readCompressedBytes\(PacketStreamImpl&\s+stream,\s*unsigned\s+char\*\s+data,\s*unsigned\s+int\s+sizeInBits\).*stream\.readBits\(data,\s*\(currentByte\s+\+\s+1\)\s*<<\s*3,\s*true\).*PacketStream::WriteRaw\(const\s+void\*\s+data,\s*std::size_t\s+size\).*doEndianSwap\(\).*reverseBytes\(bytes,\s*output\.data\(\),\s*size\);.*mImpl->writeBits\(output\.data\(\),\s*sizeInBits,\s*true\);.*PacketStream::ReadRaw\(void\*\s+data,\s*std::size_t\s+size\).*mImpl->readBits\(output\.data\(\),\s*sizeInBits,\s*true\).*reverseBytes\(output\.data\(\),\s*bytes,\s*size\);.*PacketStream::WriteCompressedRaw\(const\s+void\*\s+data,\s*std::size_t\s+size\).*writeCompressedBytes\(\*mImpl,\s*bytes,\s*sizeInBits\).*PacketStream::ReadCompressedRaw\(void\*\s+data,\s*std::size_t\s+size\).*readCompressedBytes\(\*mImpl,\s*bytes,\s*sizeInBits\)' `
    -Missing $missing

Test-Pattern -Name "PacketStream tests pin unaligned raw and compressed TES3MP serialization" -Text $basePacketTests `
    -Pattern 'TEST\(MpPacketStreamTest,\s*roundTripsBytesAcrossUnalignedBitBoundaries\).*stream\.Write\(true\).*stream\.Write\(sentShort\).*stream\.Write\(false\).*stream\.Write\(sentBytes\.data\(\).*EXPECT_EQ\(stream\.size\(\),\s*6u\).*ASSERT_TRUE\(stream\.Read\(receivedShort\)\).*ASSERT_TRUE\(stream\.Read\(receivedBytes\.data\(\).*TEST\(MpPacketStreamTest,\s*roundTripsCompressedValuesAcrossUnalignedBitBoundaries\).*stream\.Write\(true\).*stream\.WriteCompressed\(sentSparse\).*stream\.WriteCompressed\(sentFloat\).*stream\.WriteCompressed\(sentDouble\).*ASSERT_TRUE\(stream\.ReadCompressed\(receivedSparse\)\).*EXPECT_NEAR\(receivedFloat,\s*sentFloat.*TEST\(MpPacketStreamTest,\s*readPastEndFailsWithoutMutatingValue\).*EXPECT_FALSE\(stream\.Read\(extraValue\)\).*EXPECT_EQ\(extraValue,\s*0xAA\);' `
    -Missing $missing

Test-Pattern -Name "PacketStream tests pin reset pointer reuse semantics" -Text $basePacketTests `
    -Pattern 'TEST\(MpPacketStreamTest,\s*resetPointersAllowSafeStreamReuse\).*stream\.ResetReadPointer\(\).*ASSERT_TRUE\(stream\.Read\(firstRead\)\).*stream\.ResetReadPointer\(\).*ASSERT_TRUE\(stream\.Read\(firstRead\)\).*stream\.ResetWritePointer\(\).*stream\.Write\(replacementValue\).*EXPECT_EQ\(stream\.size\(\),\s*1u\).*stream\.ResetReadPointer\(\).*ASSERT_TRUE\(stream\.Read\(replacementRead\)\).*EXPECT_FALSE\(stream\.Read\(staleRead\)\).*stream\.Reset\(\).*EXPECT_EQ\(stream\.size\(\),\s*0u\).*EXPECT_FALSE\(stream\.Read\(staleRead\)\);' `
    -Missing $missing

Test-Pattern -Name "PacketStream tests pin zero-byte reads after unaligned bits" -Text ($packetStreamSource + "`n" + $basePacketTests) `
    -Pattern 'if\s*\(numberOfBitsToRead\s*==\s*0\)\s*return\s+true;.*TEST\(MpPacketStreamTest,\s*zeroByteReadSucceedsAfterUnalignedBitRead\).*stream\.Write\(true\).*stream\.Write\(sentBytes\.data\(\).*ASSERT_TRUE\(stream\.Read\(prefix\)\).*EXPECT_TRUE\(stream\.Read\(&unchanged,\s*0\)\).*EXPECT_EQ\(unchanged,\s*''x''\).*ASSERT_TRUE\(stream\.Read\(receivedBytes\.data\(\)' `
    -Missing $missing

Test-Pattern -Name "PacketStream tests pin empty input and zero-length no-op behavior" -Text ($packetStreamSource + "`n" + $basePacketTests) `
    -Pattern 'PacketStreamImpl\(unsigned\s+char\*\s+data,\s*unsigned\s+int\s+lengthInBytes\).*if\s*\(data\s*==\s*nullptr\s*\|\|\s*lengthInBytes\s*==\s*0\)\s+return;.*mData\.assign\(data,\s*data\s*\+\s*lengthInBytes\).*bool\s+readBytes\(char\*\s+output,\s*unsigned\s+int\s+numberOfBytes\).*if\s*\(numberOfBytes\s*==\s*0\)\s+return\s+true;.*PacketStream::WriteRaw\(const\s+void\*\s+data,\s*std::size_t\s+size\).*if\s*\(size\s*==\s*0\)\s+return;.*PacketStream::ReadRaw\(void\*\s+data,\s*std::size_t\s+size\).*if\s*\(size\s*==\s*0\)\s+return\s+true;.*PacketStream::WriteCompressedRaw\(const\s+void\*\s+data,\s*std::size_t\s+size\).*if\s*\(size\s*==\s*0\)\s+return;.*PacketStream::ReadCompressedRaw\(void\*\s+data,\s*std::size_t\s+size\).*if\s*\(size\s*==\s*0\)\s+return\s+true;.*TEST\(MpPacketStreamTest,\s*emptyInputAndZeroLengthOperationsAreSafeNoops\).*PacketStream\s+empty\(nullptr,\s*0\).*EXPECT_TRUE\(empty\.Read\(&unchanged,\s*0\)\).*EXPECT_FALSE\(empty\.Read\(unchanged\)\).*stream\.Write\(value,\s*0\).*EXPECT_TRUE\(stream\.Read\(received,\s*0\)\);' `
    -Missing $missing

Test-Pattern -Name "Map and list packets reject oversized counts before resize or insertion loops" -Text ($packetAndMasterTree + "`n" + $basePacketTests) `
    -Pattern '(?=.*constexpr\s+uint32_t\s+maxClientLocals\s*=\s*3000;.*clientLocalsCount\s*>\s*maxClientLocals.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxClientGlobals\s*=\s*3000;.*clientGlobalsCount\s*>\s*maxClientGlobals.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxSynchronizedClientScriptIds\s*=\s*3000;.*clientScriptsCount\s*>\s*maxSynchronizedClientScriptIds.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxSynchronizedClientGlobalIds\s*=\s*3000;.*clientGlobalsCount\s*>\s*maxSynchronizedClientGlobalIds.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxDestinationOverrides\s*=\s*3000;.*destinationCount\s*>\s*maxDestinationOverrides.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxEnforcedCollisionRefIds\s*=\s*3000;.*enforcedCollisionCount\s*>\s*maxEnforcedCollisionRefIds.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxKillChanges\s*=\s*3000;.*killChangesCount\s*>\s*maxKillChanges.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxMapTileChanges\s*=\s*3000;.*changesCount\s*>\s*maxMapTileChanges.*packetValid\s*=\s*false)(?=.*imageDataSize\s*>\s*mwmp::maxImageDataSize.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxCellsToReset\s*=\s*3000;.*cellCount\s*>\s*maxCellsToReset.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxGameSettings\s*=\s*3000;.*gameSettingCount\s*>\s*maxGameSettings.*packetValid\s*=\s*false)(?=.*constexpr\s+uint32_t\s+maxVrSettings\s*=\s*3000;.*vrSettingCount\s*>\s*maxVrSettings.*packetValid\s*=\s*false)(?=.*worldstate->recordsCount\s*>\s*maxRecords.*packetValid\s*=\s*false)(?=.*effectCount\s*>\s*maxEffects.*packetValid\s*=\s*false)(?=.*partCount\s*>\s*maxParts.*packetValid\s*=\s*false)(?=.*itemCount\s*>\s*maxItems.*packetValid\s*=\s*false)(?=.*objectClientScriptLocalRejectsOversizedCountBeforeResize)(?=.*worldstateListPacketsRejectOversizedCountsBeforeResize)(?=.*worldMapRejectsOversizedImageData)(?=.*settingsMapPacketsRejectOversizedCountsBeforeLoop)(?=.*recordDynamicRejectsOversizedCountsBeforeResize)' `
    -Missing $missing

Test-Pattern -Name "Map, actor, object, and container packets fail closed on truncated count reads" -Text ($packetAndMasterTree + "`n" + $basePacketTests) `
    -Pattern '(?=.*uint32_t\s+count\s*=\s*0;.*if\s*\(!RW\(count,\s*send\)\)\s*return;)(?=.*actorList->count\s*=\s*0;.*if\s*\(!RW\(actorList->count,\s*send\)\))(?=.*objectList->baseObjectCount\s*=\s*0;.*if\s*\(!RW\(objectList->baseObjectCount,\s*send\)\))(?=.*baseObject\.containerItemCount\s*=\s*0;.*!RW\(baseObject\.containerItemCount,\s*send\))(?=.*uint32_t\s+gameSettingCount\s*=\s*0;.*if\s*\(!RW\(gameSettingCount,\s*send\)\))(?=.*uint32_t\s+effectCount\s*=\s*0;.*if\s*\(!RW\(effectCount,\s*send\)\))(?=.*playerCompactPacketsRejectTruncatedCountsAndIndexes)(?=.*playerListPacketsRejectTruncatedCountsBeforeResize)(?=.*actorPacketRejectsTruncatedHeaderCountBeforeLoop)(?=.*worldstateMapPacketsRejectTruncatedCountsAndEntries)(?=.*recordDynamicRejectsTruncatedCountsBeforeResize)(?=.*containerPacketRejectsTruncatedHeadersAndItems)' `
    -Missing $missing

Test-Pattern -Name "PacketStream exposes TES3MP-native data and size accessors" -Text ($packetStreamHeader + "`n" + $packetStreamSource + "`n" + $basePacket + "`n" + $basePacketTests + "`n" + $gnsTransportTests) `
    -Pattern 'unsigned\s+char\*\s+data\(\);.*const\s+unsigned\s+char\*\s+data\(\)\s+const;.*std::size_t\s+size\(\)\s+const;.*unsigned\s+char\*\s+PacketStream::data\(\).*const\s+unsigned\s+char\*\s+PacketStream::data\(\)\s+const.*std::size_t\s+PacketStream::size\(\)\s+const.*sTransport->send\(bsSend->data\(\),\s*bsSend->size\(\).*client\.send\(clientStream\.data\(\),\s*clientStream\.size\(\)' `
    -Missing $missing

Test-PatternAbsent -Name "PacketStream no longer exposes RakNet-shaped data accessors" -Text ($packetStreamHeader + "`n" + $packetStreamSource + "`n" + $packetAndMasterTree + "`n" + $basePacket + "`n" + $basePacketTests + "`n" + $gnsTransportTests + "`n" + $serverNetworking + "`n" + $masterClient + "`n" + $clientNetworking) `
    -Pattern 'GetData\(|GetNumberOfBytesUsed\(' `
    -Missing $missing

Test-Pattern -Name "Fast Lua/GNS wrapper runs packet stream parity tests" -Text $luaCompatWrapper `
    -Pattern '\$coreFilter\s*=\s*"Tes3mpServerLuaCompatibilityTest\.\*:TimerApiTest\.\*:ClientSettingsTest\.\*:Tes3mpEndpointTest\.\*:MpBasePacketTest\.\*:MpPacketStreamTest\.\*:GnsTransportTest\.\*-\$communityTestFilter".*&\s+\$componentsTests\s+"--gtest_filter=\$coreFilter"' `
    -Missing $missing

Test-Pattern -Name "Fast Lua/GNS wrapper can opt into headless runtime smoke without SQL persistence" -Text $luaCompatWrapper `
    -Pattern '\[switch\]\$RunRuntimeSmoke.*\[switch\]\$RuntimeSmokeWithLocalMaster.*\[switch\]\$RuntimeSmokeWithSqlite.*RuntimeSmokeWithSqlite.*JSON-only.*function\s+Invoke-RuntimeSmoke.*scripts\\smoke-tes3mp-runtime\.ps1.*-WithLocalMaster(?!.*-WithSqlite)' `
    -Missing $missing

Test-Pattern -Name "Packet stream APIs are routed through PacketStream" -Text ($packetAndMasterTree + "`n" + $basePacketHeader + "`n" + $basePacket + "`n" + $systemPacketControllerHeader + "`n" + $playerPacketControllerHeader + "`n" + $actorPacketControllerHeader + "`n" + $objectPacketControllerHeader + "`n" + $worldstatePacketControllerHeader + "`n" + $systemPacketController + "`n" + $playerPacketController + "`n" + $actorPacketController + "`n" + $objectPacketController + "`n" + $worldstatePacketController) `
    -Pattern '(?=.*virtual\s+void\s+Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+SetReadStream\(PacketStream\s+\*bitStream\))(?=.*void\s+SetSendStream\(PacketStream\s+\*bitStream\))(?=.*void\s+SetStreams\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*PacketStream\s+\*bsRead,\s*\*bsSend,\s*\*bs;)(?=.*void\s+BasePacket::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+BasePacket::SetStreams\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*bool\s+ActorPacket::PacketHeader\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*bool\s+ObjectPacket::PacketHeader\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+PacketMasterAnnounce::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+(?:mwmp::)?PacketPreInit::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+PacketPlayerBaseInfo::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+PacketWorldTime::Packet\(PacketStream\s+\*newBitstream,\s*bool\s+send\))(?=.*void\s+mwmp::SystemPacketController::SetStream\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*void\s+mwmp::PlayerPacketController::SetStream\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*void\s+mwmp::ActorPacketController::SetStream\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*void\s+mwmp::ObjectPacketController::SetStream\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))(?=.*void\s+mwmp::WorldstatePacketController::SetStream\(PacketStream\s+\*inStream,\s*PacketStream\s+\*outStream\))' `
    -Missing $missing

Test-PatternAbsent -Name "PacketStream public header does not expose RakNet BitStream" -Text $packetStreamHeader `
    -Pattern 'RakNet::BitStream|\bBitStream\b|#include\s+<BitStream\.h>|using\s+PacketStream\s*=' `
    -Missing $missing

Test-PatternAbsent -Name "PacketStream implementation does not depend on RakNet BitStream" -Text $packetStreamSource `
    -Pattern 'RakNet::BitStream|\bBitStream\b|#include\s+<BitStream\.h>' `
    -Missing $missing

Test-PatternAbsent -Name "Packet stream APIs do not expose direct RakNet BitStream" -Text ($packetAndMasterTree + "`n" + $basePacketHeader + "`n" + $basePacket + "`n" + $systemPacketControllerHeader + "`n" + $playerPacketControllerHeader + "`n" + $actorPacketControllerHeader + "`n" + $objectPacketControllerHeader + "`n" + $worldstatePacketControllerHeader + "`n" + $systemPacketController + "`n" + $playerPacketController + "`n" + $actorPacketController + "`n" + $objectPacketController + "`n" + $worldstatePacketController) `
    -Pattern 'RakNet::BitStream|\bBitStream\b|#include\s+<BitStream\.h>|#include\s+<RakNetTypes\.h>|using\s+namespace\s+RakNet' `
    -Missing $missing

Test-PatternAbsent -Name "Component packet and GNS tests do not expose direct RakNet BitStream" -Text ($basePacketTests + "`n" + $gnsTransportTests) `
    -Pattern 'RakNet::BitStream|\bBitStream\b|#include\s+<BitStream\.h>' `
    -Missing $missing

Test-PatternAbsent -Name "Component packet and GNS tests use TES3MP packet identity aliases" -Text ($basePacketTests + "`n" + $gnsTransportTests) `
    -Pattern 'RakNet::RakNetGUID|RakNet::SystemAddress|RakNet::UNASSIGNED_|#include\s+<RakNetTypes\.h>|guid\.g' `
    -Missing $missing

Test-Pattern -Name "Packet receive ownership is TES3MP-owned at the transport boundary" -Text ($packetIdHeader + "`n" + $packetIdentityHeader + "`n" + $receivedPacketHeader + "`n" + $packetTransportHeader + "`n" + $gnsTransportHeader + "`n" + $gnsTransport + "`n" + $basePacketTests) `
    -Pattern '(?=.*using\s+PacketId\s*=\s*unsigned\s+char;)(?=.*struct\s+PacketGuid.*std::uint64_t\s+value)(?=.*struct\s+PacketAddress.*std::string\s+host;.*unsigned\s+short\s+port\s*=\s*0;)(?=.*class\s+ReceivedPacket.*ReceivedPacket\(std::vector<unsigned\s+char>\s+data,\s*PacketGuid\s+guid,\s*const\s+PacketAddress&\s+address\).*mData\(std::move\(data\)\).*mGuid\(guid\).*mAddress\(address\))(?=.*PacketId\s+id\(\)\s+const)(?=.*unsigned\s+char\*\s+data\(\)\s+const)(?=.*unsigned\s+int\s+length\(\)\s+const)(?=.*PacketGuid\s+guid\(\)\s+const)(?=.*const\s+PacketAddress&\s+address\(\)\s+const)(?=.*PacketDestination\s+destination\(\)\s+const\s*\{\s*return\s+PacketDestination\(mGuid,\s*mAddress\);)(?=.*std::vector<unsigned\s+char>\s+mData;)(?=.*PacketGuid\s+mGuid\s*=\s*unassignedPacketGuid\(\);)(?=.*PacketAddress\s+mAddress\s*=\s*unassignedPacketAddress\(\);)(?=.*virtual\s+ReceivedPacket\*\s+receive\(\)\s*=\s*0;)(?=.*virtual\s+void\s+deallocatePacket\(ReceivedPacket\*\s+packet\)\s*=\s*0;)(?=.*ReceivedPacket\*\s+receive\(\)\s+override;)(?=.*void\s+deallocatePacket\(ReceivedPacket\*\s+packet\)\s+override;)(?=.*std::deque<ReceivedPacket\*>\s+mQueuedPackets;)(?=.*ReceivedPacket\*\s+mwmp::GnsTransport::receive\(\))(?=.*void\s+mwmp::GnsTransport::deallocatePacket\(ReceivedPacket\*\s+packet\))(?=.*new\s+ReceivedPacket\(std::vector<unsigned\s+char>\{\s*id\s*\})(?=.*new\s+ReceivedPacket\(std::move\(packetData\),\s*guid,\s*address\);)(?=.*ReceivedPacket\*\s+receive\(\)\s+override\s*\{\s*return\s+nullptr;\s*\})' `
    -Missing $missing

Test-PatternAbsent -Name "Received packet storage no longer exposes a RakNet packet bridge" -Text ($receivedPacketHeader + "`n" + $packetDestinationHeader + "`n" + $gnsTransport) `
    -Pattern 'legacyPacket\(\)|RakNet::Packet\s*[\*&]|deleteData|wasGeneratedLocally|bitSize|PacketDestination\(RakNet::Packet|new\s+RakNet::Packet' `
    -Missing $missing

Test-PatternAbsent -Name "Public transport identity boundary uses TES3MP aliases" -Text ($packetDestinationHeader + "`n" + $receivedPacketHeader + "`n" + $packetTransportHeader + "`n" + $gnsTransportHeader) `
    -Pattern 'RakNet::RakNetGUID|RakNet::SystemAddress|RakNet::UNASSIGNED_|#include\s+<RakNetTypes\.h>' `
    -Missing $missing

Test-Pattern -Name "Base gameplay identity models use PacketIdentity aliases" -Text $baseIdentityHeaders `
    -Pattern '(?=.*BaseActorList.*PacketGuid\s+guid;)(?=.*struct\s+BaseObject.*PacketGuid\s+guid;)(?=.*BaseObjectList\(PacketGuid\s+guid\))(?=.*class\s+BaseObjectList.*PacketGuid\s+guid;)(?=.*BasePlayer\(PacketGuid\s+guid\))(?=.*class\s+BasePlayer.*PacketGuid\s+guid;)(?=.*std::vector<PacketGuid>\s+alliedPlayers;)(?=.*struct\s+Target.*PacketGuid\s+guid;)(?=.*BaseSystem\(PacketGuid\s+guid\))(?=.*class\s+BaseSystem.*PacketGuid\s+guid;)(?=.*class\s+BaseWorldstate.*PacketGuid\s+guid;)' `
    -Missing $missing

Test-PatternAbsent -Name "Base gameplay identity models do not expose direct RakNet GUID headers" -Text $baseIdentityHeaders `
    -Pattern 'RakNet::RakNetGUID|#include\s+<RakNetTypes\.h>' `
    -Missing $missing

Test-Pattern -Name "GNS transport parses embedded GUIDs without RakNet BitStream" -Text $gnsTransport `
    -Pattern 'PacketGuid\s+readEmbeddedGuid\(const\s+unsigned\s+char\*\s+data\).*std::uint64_t\s+value\s*=\s*0;.*for\s*\(std::size_t\s+index\s*=\s*0;\s*index\s*<\s*mwmp::packetGuidSize\(\);\s*\+\+index\).*value\s*=\s*\(value\s*<<\s*8\)\s*\|\s*data\[index\s*\+\s*1\];.*return\s+mwmp::makePacketGuid\(value\);.*const\s+auto\s+data\s*=\s*static_cast<unsigned\s+char\*>\(message->m_pData\);.*mUseEmbeddedPacketGuid\s*&&\s*data\s*!=\s*nullptr.*packetGuidSize\(\).*readEmbeddedGuid\(data\)' `
    -Missing $missing

Test-PatternAbsent -Name "GNS transport does not depend on RakNet BitStream parsing" -Text $gnsTransport `
    -Pattern 'BitStream|#include\s+<BitStream\.h>' `
    -Missing $missing

Test-PatternAbsent -Name "GNS transport keeps RakNet GUID API behind PacketIdentity" -Text $gnsTransport `
    -Pattern 'RakNet::RakNetGUID(::size|\()|RakNet::UNASSIGNED_CRABNET_GUID|RakNet::UNASSIGNED_SYSTEM_ADDRESS' `
    -Missing $missing

Test-PatternAbsent -Name "Runtime invalid GUID checks use PacketIdentity helpers" -Text ($serverNetworking + "`n" + $clientPlayerList) `
    -Pattern 'RakNet::UNASSIGNED_CRABNET_GUID' `
    -Missing $missing

Test-PatternAbsent -Name "Runtime packet identity internals stay behind PacketIdentity helpers" -Text ($serverNetworking + "`n" + $serverNetworkingHeader + "`n" + $serverPlayers + "`n" + $serverScriptFunctions + "`n" + $masterClient + "`n" + $masterClientHeader + "`n" + $clientNetworking + "`n" + $clientMechanicsHelper + "`n" + $basePacketHeader) `
    -Pattern 'guid\.g|RakNet::RakNetGUID::size\(\)|RakNet::RakNetGUID\(\)|RakNet::SystemAddress|RakNet::UNASSIGNED_SYSTEM_ADDRESS|address\(\)\.ToString|receivedPacket->address\(\)\.ToString|packet->address\(\)\.ToString|addr\.ToString|masterServer\(masterHost\.c_str\(\),\s*masterPort\)' `
    -Missing $missing

Test-Pattern -Name "Dedicated server player and cell identity APIs use PacketGuid" -Text ($serverPlayersHeader + "`n" + $serverPlayers + "`n" + $serverNetworkingHeader + "`n" + $serverNetworking + "`n" + $serverCellHeader + "`n" + $serverCell) `
    -Pattern '(?=.*typedef\s+std::map<mwmp::PacketGuid,\s*Player\*>\s+TPlayers;)(?=.*static\s+void\s+newPlayer\(mwmp::PacketGuid\s+guid\);)(?=.*void\s+Players::newPlayer\(mwmp::PacketGuid\s+guid\))(?=.*Player::Player\(mwmp::PacketGuid\s+guid\))(?=.*void\s+Networking::newPlayer\(PacketGuid\s+guid\))(?=.*void\s+Networking::disconnectPlayer\(PacketGuid\s+guid\))(?=.*void\s+Networking::kickPlayer\(PacketGuid\s+guid,\s*bool\s+sendNotification\))(?=.*mwmp::PacketGuid\s+\*getAuthority\(\);)(?=.*void\s+setAuthority\(const\s+mwmp::PacketGuid&\s+guid\);)(?=.*mwmp::PacketGuid\s+authorityGuid;)' `
    -Missing $missing

Test-PatternAbsent -Name "Dedicated server player and cell identity APIs do not expose direct RakNet GUID headers" -Text ($serverPlayersHeader + "`n" + $serverPlayers + "`n" + $serverNetworkingHeader + "`n" + $serverNetworking + "`n" + $serverCellHeader + "`n" + $serverCell) `
    -Pattern 'RakNet::RakNetGUID|#include\s+<RakNetTypes\.h>' `
    -Missing $missing

Test-Pattern -Name "OpenMW client player identity APIs use PacketGuid" -Text ($clientPlayerListHeader + "`n" + $clientPlayerList + "`n" + $clientDedicatedPlayerHeader + "`n" + $clientDedicatedPlayer + "`n" + $clientWorldstate + "`n" + $clientMechanicsHelper + "`n" + $clientProcessorPlayerAlly + "`n" + $clientProcessorUserDisconnected) `
    -Pattern '(?=.*static\s+DedicatedPlayer\s*\*\s*newPlayer\(PacketGuid\s+guid\);)(?=.*static\s+void\s+deletePlayer\(PacketGuid\s+guid\);)(?=.*static\s+DedicatedPlayer\s*\*\s*getPlayer\(PacketGuid\s+guid\);)(?=.*static\s+std::vector<PacketGuid>\s+getPlayersInCell\(const\s+ESM::Cell&\s+cell\);)(?=.*static\s+std::map<PacketGuid,\s+DedicatedPlayer\s*\*>\s+playerList;)(?=.*std::map<PacketGuid,\s+DedicatedPlayer\s*\*>\s+PlayerList::playerList;)(?=.*DedicatedPlayer\s*\*\s*PlayerList::newPlayer\(PacketGuid\s+guid\))(?=.*void\s+PlayerList::deletePlayer\(PacketGuid\s+guid\))(?=.*DedicatedPlayer\s*\*\s*PlayerList::getPlayer\(PacketGuid\s+guid\))(?=.*std::vector<PacketGuid>\s+PlayerList::getPlayersInCell\(const\s+ESM::Cell&\s+cell\))(?=.*DedicatedPlayer\(PacketGuid\s+guid\);)(?=.*DedicatedPlayer::DedicatedPlayer\(PacketGuid\s+guid\))(?=.*std::vector<PacketGuid>\s+playersInResetCell;)(?=.*PacketGuid\s+playerCheckedGuid;)(?=.*std::vector<PacketGuid>::iterator)' `
    -Missing $missing

Test-Pattern -Name "OpenMW client cell and marker identity APIs use PacketGuid" -Text ($clientCellHeader + "`n" + $clientCell + "`n" + $clientGuiControllerHeader + "`n" + $clientGuiController) `
    -Pattern '(?=.*void\s+setAuthority\(const\s+PacketGuid&\s+guid\);)(?=.*PacketGuid\s+authorityGuid;)(?=.*void\s+Cell::setAuthority\(const\s+PacketGuid&\s+guid\))(?=.*ESM::CustomMarker\s+createMarker\(const\s+PacketGuid\s+&guid\);)(?=.*ESM::CustomMarker\s+mwmp::GUIController::createMarker\(const\s+PacketGuid\s+&guid\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW client GUID logging uses PacketIdentity formatting" -Text ($clientPlayerList + "`n" + $clientGuiController) `
    -Pattern 'PlayerList::newPlayer\(PacketGuid\s+guid\).*packetGuidToString\(guid\)\.c_str\(\).*GUIController::createMarker\(const\s+PacketGuid\s+&guid\).*packetGuidToString\(guid\)\.c_str\(\)' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW client migrated player identity APIs do not expose direct RakNet GUID headers" -Text ($clientPlayerListHeader + "`n" + $clientPlayerList + "`n" + $clientDedicatedPlayerHeader + "`n" + $clientDedicatedPlayer + "`n" + $clientWorldstate + "`n" + $clientMechanicsHelper + "`n" + $clientProcessorPlayerAlly + "`n" + $clientProcessorUserDisconnected + "`n" + $clientBaseIdentityHeaders + "`n" + $clientCellHeader + "`n" + $clientCell + "`n" + $clientGuiControllerHeader + "`n" + $clientGuiController) `
    -Pattern 'RakNet::RakNetGUID|#include\s+<RakNetTypes\.h>' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW client migrated GUID logging does not call direct RakNet formatting" -Text ($clientPlayerList + "`n" + $clientGuiController) `
    -Pattern '\bguid\.ToString\(\)' `
    -Missing $missing

Test-PatternAbsent -Name "GNS transport reads packet GUID values only through PacketIdentity" -Text $gnsTransport `
    -Pattern '\.g\b' `
    -Missing $missing

Test-PatternAbsent -Name "GNS transport formats packet addresses only through PacketIdentity" -Text $gnsTransport `
    -Pattern 'destination\.address\(\)\.ToString|return\s+address\.ToString\(false\)|address\.ToString\(true,\s*buffer' `
    -Missing $missing

Test-PatternAbsent -Name "GNS transport system packet queue uses TES3MP packet ids" -Text ($gnsTransportHeader + "`n" + $gnsTransport) `
    -Pattern 'RakNet::MessageID|#include\s+<MessageIdentifiers\.h>' `
    -Missing $missing

Test-Pattern -Name "GNS consumes packet destination state through wrapper helpers" -Text $gnsTransport `
    -Pattern 'destination\.hasGuid\(\)\s*&&\s*guid\s*==\s*destination\.guid\(\).*destination\.hasAddress\(\)\s*&&\s*addressForConnection\(connection\)\s*==\s*destination\.address\(\).*if\s*\(destination\.hasGuid\(\)\).*mGuidConnections\.find\(packetGuidValue\(destination\.guid\(\)\)\).*if\s*\(destination\.hasAddress\(\)\).*mAddressConnections\.find\(addressKey\(destination\.address\(\)\)\)' `
    -Missing $missing

Test-Pattern -Name "GNS duplicate embedded client GUID replaces stale connection ownership" -Text ($gnsTransport + "`n" + $gnsTransportTests) `
    -Pattern 'void\s+mwmp::GnsTransport::setConnectionGuid\(HSteamNetConnection\s+connection,\s*PacketGuid\s+guid\).*claimedGuid\s*=\s*mGuidConnections\.find\(packetGuidValue\(guid\)\).*claimedGuid\s*!=\s*mGuidConnections\.end\(\)\s*&&\s*claimedGuid->second\s*!=\s*connection.*CloseConnection\(previousConnection,\s*connectionCloseReason,\s*"CommunityMP duplicate client GUID",\s*true\).*forgetConnection\(previousConnection\).*mConnectionGuids\[connection\]\s*=\s*guid;.*mGuidConnections\[packetGuidValue\(guid\)\]\s*=\s*connection;.*TEST\(GnsTransportTest,\s*duplicateEmbeddedClientGuidReplacesOldConnection\).*sendClientPreInitAndGetAddress\(\*server,\s*clientB,\s*sharedGuid\).*EXPECT_EQ\(server->numberOfConnections\(\),\s*1\);.*EXPECT_EQ\(server->send\(broadcastPayload.*PacketDestination\(\),\s*true\),\s*1\);' `
    -Missing $missing

Test-PatternAbsent -Name "Packet destination does not accept, store, or expose raw AddressOrGUID" -Text ($packetDestinationHeader + "`n" + $gnsTransport + "`n" + $basePacketHeader + "`n" + $basePacket + "`n" + $serverNetworkingHeader + "`n" + $serverNetworking + "`n" + $basePacketTests + "`n" + $gnsTransportTests) `
    -Pattern 'AddressOrGUID|raw\(\)|mDestination|RakNet::AddressOrGUID\s+m' `
    -Missing $missing

Test-PatternAbsent -Name "Active packet transport boundary has no direct RakNet AddressOrGUID dependency" -Text ($packetTransportHeader + "`n" + $gnsTransportHeader + "`n" + $basePacketHeader + "`n" + $serverNetworkingHeader) `
    -Pattern 'AddressOrGUID' `
    -Missing $missing

Test-PatternAbsent -Name "Active TES3MP packet delivery has no direct PacketPriority.h dependency" -Text ($packetTransportHeader + "`n" + $basePacketHeader + "`n" + $basePacket + "`n" + $actorPacket + "`n" + $objectPacket + "`n" + $playerPacket + "`n" + $systemPacket + "`n" + $worldstatePacket + "`n" + $gnsTransport) `
    -Pattern '#include\s+<PacketPriority\.h>|::PacketPriority|::PacketReliability' `
    -Missing $missing

Test-PatternAbsent -Name "Packet controllers have no RakPeer constructor plumbing" -Text ($systemPacketControllerHeader + "`n" + $playerPacketControllerHeader + "`n" + $actorPacketControllerHeader + "`n" + $objectPacketControllerHeader + "`n" + $worldstatePacketControllerHeader + "`n" + $systemPacketController + "`n" + $playerPacketController + "`n" + $actorPacketController + "`n" + $objectPacketController + "`n" + $worldstatePacketController + "`n" + $serverNetworking + "`n" + $clientNetworking) `
    -Pattern 'RakPeerInterface|#include\s+<RakPeerInterface\.h>|AddPacket\([^)]*peer|new\s+T\(peer\)|Controller\(nullptr\)|Controller\(RakNet::RakPeerInterface' `
    -Missing $missing

Test-Pattern -Name "Base packets expose TES3MP packet ids" -Text ($packetIdHeader + "`n" + $basePacketHeader) `
    -Pattern 'using\s+PacketId\s*=\s*unsigned\s+char;.*PacketId\s+GetPacketID\(\)\s+const' `
    -Missing $missing

Test-Pattern -Name "TES3MP packet ids own legacy connection status and game packet boundaries" -Text ($packetIdHeader + "`n" + $networkMessagesHeader) `
    -Pattern 'ID_CONNECTED_PING\s*=\s*0,.*ID_UNCONNECTED_PING\s*=\s*1,.*ID_SND_RECEIPT_ACKED\s*=\s*14,.*ID_SND_RECEIPT_LOSS\s*=\s*15,.*ID_CONNECTION_REQUEST_ACCEPTED\s*=\s*16,.*ID_CONNECTION_ATTEMPT_FAILED\s*=\s*17,.*ID_ALREADY_CONNECTED\s*=\s*18,.*ID_NEW_INCOMING_CONNECTION\s*=\s*19,.*ID_NO_FREE_INCOMING_CONNECTIONS\s*=\s*20,.*ID_DISCONNECTION_NOTIFICATION\s*=\s*21,.*ID_CONNECTION_LOST\s*=\s*22,.*ID_CONNECTION_BANNED\s*=\s*23,.*ID_INVALID_PASSWORD\s*=\s*24,.*ID_INCOMPATIBLE_PROTOCOL_VERSION\s*=\s*25,.*ID_REMOTE_DISCONNECTION_NOTIFICATION\s*=\s*31,.*ID_REMOTE_CONNECTION_LOST\s*=\s*32,.*ID_REMOTE_NEW_INCOMING_CONNECTION\s*=\s*33,.*ID_USER_PACKET_ENUM\s*=\s*134.*enum\s+GameMessages\s*:\s*mwmp::PacketId.*_ID_UNUSED\s*=\s*ID_USER_PACKET_ENUM\+1' `
    -Missing $missing

Test-PatternAbsent -Name "TES3MP packet id definitions do not include RakNet message identifiers" -Text ($packetIdHeader + "`n" + $networkMessagesHeader) `
    -Pattern '#include\s+<MessageIdentifiers\.h>' `
    -Missing $missing

Test-Pattern -Name "System packet controller dispatch uses TES3MP packet ids" -Text ($systemPacketControllerHeader + "`n" + $systemPacketController) `
    -Pattern 'SystemPacket\s+\*GetPacket\(PacketId\s+id\);.*bool\s+ContainsPacket\(PacketId\s+id\);.*typedef\s+std::unordered_map<PacketId,\s*std::unique_ptr<SystemPacket>\s*>\s+packets_t;.*SystemPacketController::GetPacket\(PacketId\s+id\).*SystemPacketController::ContainsPacket\(PacketId\s+id\)' `
    -Missing $missing

Test-Pattern -Name "Player packet controller dispatch uses TES3MP packet ids" -Text ($playerPacketControllerHeader + "`n" + $playerPacketController) `
    -Pattern 'PlayerPacket\s+\*GetPacket\(PacketId\s+id\);.*bool\s+ContainsPacket\(PacketId\s+id\);.*typedef\s+std::unordered_map<PacketId,\s*std::unique_ptr<PlayerPacket>\s*>\s+packets_t;.*PlayerPacketController::GetPacket\(PacketId\s+id\).*PlayerPacketController::ContainsPacket\(PacketId\s+id\)' `
    -Missing $missing

Test-Pattern -Name "Actor packet controller dispatch uses TES3MP packet ids" -Text ($actorPacketControllerHeader + "`n" + $actorPacketController) `
    -Pattern 'ActorPacket\s+\*GetPacket\(PacketId\s+id\);.*bool\s+ContainsPacket\(PacketId\s+id\);.*typedef\s+std::unordered_map<PacketId,\s*std::unique_ptr<ActorPacket>\s*>\s+packets_t;.*ActorPacketController::GetPacket\(PacketId\s+id\).*ActorPacketController::ContainsPacket\(PacketId\s+id\)' `
    -Missing $missing

Test-Pattern -Name "Object packet controller dispatch uses TES3MP packet ids" -Text ($objectPacketControllerHeader + "`n" + $objectPacketController) `
    -Pattern 'ObjectPacket\s+\*GetPacket\(PacketId\s+id\);.*bool\s+ContainsPacket\(PacketId\s+id\);.*typedef\s+std::unordered_map<PacketId,\s*std::unique_ptr<ObjectPacket>\s*>\s+packets_t;.*ObjectPacketController::GetPacket\(PacketId\s+id\).*ObjectPacketController::ContainsPacket\(PacketId\s+id\)' `
    -Missing $missing

Test-Pattern -Name "Worldstate packet controller dispatch uses TES3MP packet ids" -Text ($worldstatePacketControllerHeader + "`n" + $worldstatePacketController) `
    -Pattern 'WorldstatePacket\s+\*GetPacket\(PacketId\s+id\);.*bool\s+ContainsPacket\(PacketId\s+id\);.*typedef\s+std::unordered_map<PacketId,\s*std::unique_ptr<WorldstatePacket>\s*>\s+packets_t;.*WorldstatePacketController::GetPacket\(PacketId\s+id\).*WorldstatePacketController::ContainsPacket\(PacketId\s+id\)' `
    -Missing $missing

Test-Pattern -Name "Networking and GNS tests dispatch packet ids without RakNet MessageID" -Text ($serverNetworking + "`n" + $clientNetworkingHeader + "`n" + $clientNetworking + "`n" + $gnsTransportTests) `
    -Pattern 'sendConnectionStatus\(PacketTransport\*\s+transport,\s*ReceivedPacket\*\s+packet,\s*PacketId\s+messageId\).*SystemPacket\s+\*getSystemPacket\(PacketId\s+id\).*PlayerPacket\s+\*getPlayerPacket\(PacketId\s+id\).*ActorPacket\s+\*getActorPacket\(PacketId\s+id\).*ObjectPacket\s+\*getObjectPacket\(PacketId\s+id\).*WorldstatePacket\s+\*getWorldstatePacket\(PacketId\s+id\).*std::vector<PacketId>\s+receivePacketIds' `
    -Missing $missing

Test-PatternAbsent -Name "Packet controller and GNS test dispatch has no direct RakNet message id dependency" -Text ($systemPacketControllerHeader + "`n" + $playerPacketControllerHeader + "`n" + $actorPacketControllerHeader + "`n" + $objectPacketControllerHeader + "`n" + $worldstatePacketControllerHeader + "`n" + $systemPacketController + "`n" + $playerPacketController + "`n" + $actorPacketController + "`n" + $objectPacketController + "`n" + $worldstatePacketController + "`n" + $serverNetworking + "`n" + $clientNetworkingHeader + "`n" + $clientNetworking + "`n" + $gnsTransportTests) `
    -Pattern 'RakNet::MessageID|#include\s+<MessageIdentifiers\.h>' `
    -Missing $missing

Test-Pattern -Name "BasePacket transport-only send has component coverage" -Text $basePacketTests `
    -Pattern 'class\s+CapturingTransport\s+final\s*:\s*public\s+mwmp::PacketTransport.*TEST\(MpBasePacketTest,\s*sendUsesPacketTransportWithoutRakPeerFallback\).*ScopedPacketTransport\s+scopedTransport\(&transport\).*mwmp::PacketSystemHandshake\s+packet.*EXPECT_EQ\(packet\.Send\(destination\),\s*1u\).*EXPECT_EQ\(transport\.sentData\[0\],\s*ID_SYSTEM_HANDSHAKE\).*EXPECT_EQ\(transport\.sentDestination\.guid\(\),\s*testGuid\(\)\);' `
    -Missing $missing

Test-Pattern -Name "OpenMW sync automation configures required GNS" -Text $updateOpenMw `
    -Pattern '\[switch\]\$InstallGnsDependencies,.*if\s*\(\$InstallGnsDependencies\)\s*\{.*Initialize-GnsVcpkg\s+\$repoRoot.*"-D",\s+"BUILD_TES3MP_MASTER=ON".*"-D",\s+"BUILD_COMPONENTS_TESTS=ON"' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW sync automation no longer exposes a GNS disable switch" -Text $updateOpenMw `
    -Pattern '\[switch\]\$(UseGns|DisableGns)|TES3MP_USE_GNS|useGnsForBuild' `
    -Missing $missing

Test-PatternAbsent -Name "Scheduled OpenMW sync workflow no longer passes legacy GNS switches" -Text $openMwSyncWorkflow `
    -Pattern '"-(UseGns|DisableGns)"' `
    -Missing $missing

Test-PatternAbsent -Name "Windows CI has no non-GNS TES3MP workflow input" -Text $windowsWorkflow `
    -Pattern 'tes3mp-gns\s*:' `
    -Missing $missing

Test-Pattern -Name "Windows CI prepares and validates TES3MP GNS unconditionally" -Text $windowsWorkflow `
    -Pattern '- name:\s+Prepare TES3MP GNS dependencies.*gamenetworkingsockets boost-asio boost-iostreams.*CMAKE_PREFIX_PATH=.*vcpkg-gns.*BUILD_TES3MP_MASTER=ON.*- name:\s+Smoke TES3MP GNS runtime\s+if:\s*\$\{\{\s*! inputs\.package\s*\}\}' `
    -Missing $missing

Test-PatternAbsent -Name "Windows CI does not request disabled SQLite runtime smoke" -Text $windowsWorkflow `
    -Pattern '-WithSqlite' `
    -Missing $missing

Test-Pattern -Name "Ubuntu CI prepares and builds TES3MP GNS and master coverage" -Text $pushWorkflow `
    -Pattern '- name:\s+Prepare TES3MP GNS dependencies\s+run:\s*\|.*vcpkg install gamenetworkingsockets boost-asio boost-iostreams --triplet x64-linux.*-D BUILD_TES3MP_MASTER=ON.*-D CMAKE_PREFIX_PATH=\$\{\{\s*github\.workspace\s*\}\}/deps/vcpkg-gns/installed/x64-linux' `
    -Missing $missing

Test-Pattern -Name "macOS CI prepares TES3MP GNS dependencies for both runner architectures" -Text $macosWorkflow `
    -Pattern '- name:\s+Prepare TES3MP GNS dependencies\s+run:\s*\|.*triplet=x64-osx-dynamic.*triplet=arm64-osx-dynamic.*vcpkg install gamenetworkingsockets boost-asio boost-iostreams --triplet "\$triplet".*GNS_VCPKG_ROOT=.*deps/vcpkg-gns' `
    -Missing $missing

Test-Pattern -Name "macOS configure builds TES3MP GNS and master coverage" -Text $macosScript `
    -Pattern '-D BUILD_TES3MP_MASTER=TRUE.*GNS_VCPKG_ROOT.*CMAKE_PREFIX_PATH_VALUE="\$CMAKE_PREFIX_PATH_VALUE;\$GNS_VCPKG_ROOT/installed/\$VCPKG_TARGET_TRIPLET"' `
    -Missing $missing

Test-Pattern -Name "Pre-init packet carries version, protocol, commit hash, bounded plugin count, and checksum metadata" -Text $packetPreInit `
    -Pattern 'PacketPreInit::PacketPreInit\(\).*packetID\s*=\s*ID_GAME_PREINIT;.*if\s*\(checksums\s*==\s*nullptr\).*packetValid\s*=\s*false;.*RW\(version,\s*send,\s*false,\s*versionMaxLength\).*RW\(protocolVersion,\s*send\).*RW\(commitHash,\s*send,\s*false,\s*commitHashMaxLength\).*RW\(numberOfChecksums,\s*send\).*if\s*\(numberOfChecksums\s*>\s*maxPlugins\).*packetValid\s*=\s*false;.*if\s*\(nas\.strSize\s*>\s*pluginNameMaxLength\).*else\s+if\s*\(nas\.hashN\s*>\s*maxHashes\).*packetValid\s*=\s*false;' `
    -Missing $missing

Test-Pattern -Name "Empty server pre-init response clears stale client checksum data while preserving protocol info" -Text $packetPreInit `
    -Pattern 'if\s*\(numberOfChecksums\s*==\s*0\).*server\s+accepted\s+plugin\s+list.*if\s*\(!send\)\s+checksums->clear\(\);.*return;' `
    -Missing $missing

Test-Pattern -Name "Server rejects non-pre-init first packets before creating a player" -Text $serverNetworking `
    -Pattern 'bool\s+Networking::preInit\(ReceivedPacket\*\s+packet,\s+PacketStream\s+&bsIn\).*if\s*\(packet->id\(\)\s*!=\s*ID_GAME_PREINIT\)\s*\{.*sent\s+wrong\s+first\s+packet.*transport->closeConnection\(packet->destination\(\),\s*true\).*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "Duplicate pre-init for an existing GUID replaces the stale player session before handshake" -Text $serverNetworking `
    -Pattern 'PacketStream\s+bsIn\(&receivedPacket->data\(\)\[1\],\s*receivedPacket->length\(\)\);.*if\s*\(receivedPacket->id\(\)\s*==\s*ID_GAME_PREINIT\)\s*\{.*if\s*\(Players::doesPlayerExist\(receivedPacket->guid\(\)\)\)\s*\{.*duplicate\s+ID_GAME_PREINIT.*disconnectPlayer\(receivedPacket->guid\(\)\);.*preInit\(receivedPacket,\s*bsIn\);.*\}\s*else\s+if\s*\(Players::doesPlayerExist\(receivedPacket->guid\(\)\)\)\s*update\(receivedPacket,\s*bsIn\);.*else\s+preInit\(receivedPacket,\s*bsIn\);' `
    -Missing $missing

Test-Pattern -Name "Server rejects invalid or empty pre-init packets with a status packet and closes the transport" -Text $serverNetworking `
    -Pattern 'packetPreInit\.Read\(\);.*if\s*\(!packetPreInit\.isPacketValid\(\)\s*\|\|\s*dataFiles\.empty\(\)\)\s*\{.*sendConnectionStatus\(transport,\s*packet,\s*ID_INVALID_PASSWORD\);.*transport->closeConnection\(packet->destination\(\),\s*true\).*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "Server rejects protocol-version mismatches with ID_INCOMPATIBLE_PROTOCOL_VERSION and closes" -Text $serverNetworking `
    -Pattern 'if\s*\(packetPreInit\.getProtocolVersion\(\)\s*!=\s*expectedProtocolVersion\)\s*\{.*protocol\s+version\s+mismatch.*sendConnectionStatus\(transport,\s*packet,\s*ID_INCOMPATIBLE_PROTOCOL_VERSION\);.*transport->closeConnection\(packet->destination\(\),\s*true\).*return\s+false;' `
    -Missing $missing

Test-Pattern -Name "Server logs build metadata mismatches but allows matching protocol clients" -Text $serverNetworking `
    -Pattern 'void\s+logCompatibleBuildMetadataDifference\(const\s+PacketPreInit&\s+packetPreInit,\s*const\s+std::string&\s+expectedVersion,\s*const\s+std::string&\s+expectedCommitHash\).*packetPreInit\.getVersion\(\)\s*==\s*expectedVersion\s*&&\s*packetPreInit\.getCommitHash\(\)\s*==\s*expectedCommitHash.*return;.*Client build metadata differs, but protocol version matches; allowing connection.*Client version:.*Client commit:.*if\s*\(packetPreInit\.getProtocolVersion\(\)\s*!=\s*expectedProtocolVersion\).*return\s+false;.*logCompatibleBuildMetadataDifference\(packetPreInit,\s*expectedVersion,\s*expectedCommitHash\);' `
    -Missing $missing

Test-Pattern -Name "Client accepts server pre-init metadata differences when protocol matches" -Text $clientNetworking `
    -Pattern 'else\s+if\s*\(packetPreInit\.getVersion\(\)\s*!=\s*TES3MP_VERSION\s*\|\|\s*packetPreInit\.getCommitHash\(\)\s*!=\s*commitHashString\).*Server build metadata differs, but protocol version matches; allowing connection.*Client version:.*server version:.*Client commit:.*server commit:' `
    -Missing $missing

Test-Pattern -Name "Server compares client data files case-insensitively and accepts any configured checksum alternative" -Text $serverNetworking `
    -Pattern 'if\s*\(samples\.size\(\)\s*==\s*dataFiles\.size\(\)\).*Misc::StringUtils::ciEqual\(samples\[i\]\.first,\s*dataFile->first\).*if\s*\(hashList\.empty\(\)\)\s+continue;.*auto\s+it\s*=\s*find\(hashList\.begin\(\),\s*hashList\.end\(\),\s*dataFile->second\[0\]\);.*if\s*\(it\s*==\s*hashList\.end\(\)\)\s+break;' `
    -Missing $missing

Test-Pattern -Name "Server sends authoritative sample checksums and closes on data-file mismatch when enforcement is enabled" -Text $serverNetworking `
    -Pattern 'if\s*\(dataFileEnforcementState\s*&&\s*dataFile\s*!=\s*dataFiles\.end\(\)\)\s*\{.*incompatible\s+data\s+files.*packetPreInit\.setChecksums\(&samples\);.*packetPreInit\.setProtocolVersionInfo\(expectedVersion,\s*expectedProtocolVersion,\s*expectedCommitHash\);.*packetPreInit\.Send\(packet->address\(\)\);.*transport->closeConnection\(packet->destination\(\),\s*true\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated server receive and gameplay processors consume received packet wrappers" -Text ($serverNetworkingHeader + "`n" + $serverNetworking + "`n" + $serverPlayerProcessorHeader + "`n" + $serverPlayerProcessor + "`n" + $serverActorProcessorHeader + "`n" + $serverActorProcessor + "`n" + $serverObjectProcessorHeader + "`n" + $serverObjectProcessor + "`n" + $serverWorldstateProcessorHeader + "`n" + $serverWorldstateProcessor) `
    -Pattern 'void\s+sendConnectionStatus\(PacketTransport\*\s+transport,\s*ReceivedPacket\*\s+packet,\s*PacketId\s+messageId\).*packet->destination\(\).*bool\s+Networking::preInit\(ReceivedPacket\*\s+packet,\s+PacketStream\s+&bsIn\).*void\s+Networking::update\(ReceivedPacket\*\s+packet,\s+PacketStream\s+&bsIn\).*systemPacketController->ContainsPacket\(packet->id\(\)\).*processSystemPacket\(packet\).*PacketStream\s+bsIn\(&receivedPacket->data\(\)\[1\],\s*receivedPacket->length\(\)\).*update\(receivedPacket,\s*bsIn\).*preInit\(receivedPacket,\s*bsIn\).*bool\s+PlayerProcessor::Process\(ReceivedPacket&\s+packet\).*GetPacket\(packet\.id\(\)\).*bool\s+ActorProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseActorList\s*&\s*actorList\).*bool\s+ObjectProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseObjectList\s*&\s*objectList\).*bool\s+WorldstateProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseWorldstate\s*&\s*worldstate\)' `
    -Missing $missing

Test-Pattern -Name "Dedicated server ignores gameplay packets from missing player sessions" -Text ($serverNetworking + "`n" + $serverPlayerProcessor + "`n" + $serverActorProcessor + "`n" + $serverObjectProcessor + "`n" + $serverWorldstateProcessor) `
    -Pattern 'bool\s+getPlayerForGameplayPacket\(ReceivedPacket\*\s+packet,\s*Player\*&\s*player,\s*const\s+char\*\s+packetCategory\).*player\s*=\s*Players::getPlayer\(packet->guid\(\)\);.*player\s*!=\s*nullptr.*Ignoring %s packet %u from unknown player session.*void\s+Networking::processPlayerPacket\(ReceivedPacket\*\s+packet\).*if\s*\(!getPlayerForGameplayPacket\(packet,\s*player,\s*"PlayerPacket"\)\).*return;.*void\s+Networking::processActorPacket\(ReceivedPacket\*\s+packet\).*if\s*\(!getPlayerForGameplayPacket\(packet,\s*player,\s*"ActorPacket"\)\).*return;.*void\s+Networking::processObjectPacket\(ReceivedPacket\*\s+packet\).*if\s*\(!getPlayerForGameplayPacket\(packet,\s*player,\s*"ObjectPacket"\)\).*return;.*void\s+Networking::processWorldstatePacket\(ReceivedPacket\*\s+packet\).*if\s*\(!getPlayerForGameplayPacket\(packet,\s*player,\s*"WorldstatePacket"\)\).*return;.*bool\s+PlayerProcessor::Process\(ReceivedPacket&\s+packet\).*player\s*==\s*nullptr.*missing player session.*bool\s+ActorProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseActorList\s*&\s*actorList\).*player\s*==\s*nullptr.*missing player session.*bool\s+ObjectProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseObjectList\s*&\s*objectList\).*player\s*==\s*nullptr.*missing player session.*bool\s+WorldstateProcessor::Process\(ReceivedPacket&\s+packet,\s*BaseWorldstate\s*&\s*worldstate\).*player\s*==\s*nullptr.*missing player session' `
    -Missing $missing

Test-Pattern -Name "Actor object and worldstate processors drop invalid decoded packets" -Text ($serverActorProcessor + "`n" + $serverObjectProcessor + "`n" + $serverWorldstateProcessor + "`n" + $clientActorProcessor + "`n" + $clientObjectProcessor + "`n" + $clientWorldstateProcessor) `
    -Pattern 'if\s*\(actorList\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*if\s*\(objectList\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*if\s*\(worldstate\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*if\s*\(actorList\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*if\s*\(objectList\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!.*if\s*\(worldstate\.isValid\s*&&\s*myPacket->isPacketValid\(\)\).*failed integrity check and was ignored!' `
    -Missing $missing

Test-PatternAbsent -Name "Dedicated server receive path does not unwrap received packets to RakNet packets" -Text ($serverNetworkingHeader + "`n" + $serverNetworking + "`n" + $serverPlayerProcessorHeader + "`n" + $serverPlayerProcessor + "`n" + $serverActorProcessorHeader + "`n" + $serverActorProcessor + "`n" + $serverObjectProcessorHeader + "`n" + $serverObjectProcessor + "`n" + $serverWorldstateProcessorHeader + "`n" + $serverWorldstateProcessor) `
    -Pattern 'legacyPacket\(\)|preInit\(RakNet::Packet|update\(RakNet::Packet|process\w+Packet\(RakNet::Packet|Process\(RakNet::Packet|RakNet::Packet\s*[\*&]|RakNet::AddressOrGUID\(packet\)' `
    -Missing $missing

Test-Pattern -Name "Dedicated server networking requires the GNS transport for admin and receive-loop operations" -Text $serverNetworking `
    -Pattern 'PacketAddress\s+Networking::getPacketAddress\(PacketGuid\s+guid\).*return\s+transport->getPacketAddress\(guid\);.*void\s+Networking::kickPlayer\(PacketGuid\s+guid,\s*bool\s+sendNotification\).*transport->closeConnection\(PacketDestination\(guid\),\s*sendNotification\);.*void\s+Networking::banAddress\(const\s+char\s+\*ipAddress\).*transport->banAddress\(ipAddress\);.*void\s+Networking::unbanAddress\(const\s+char\s+\*ipAddress\).*transport->unbanAddress\(ipAddress\);.*unsigned\s+short\s+Networking::numberOfConnections\(\)\s+const.*return\s+transport->numberOfConnections\(\);.*int\s+Networking::getAvgPing\(const\s+PacketDestination&\s+destination\)\s+const.*return\s+transport->averagePing\(destination\);.*unsigned\s+short\s+Networking::getPort\(\)\s+const.*return\s+transport->port\(\);' `
    -Missing $missing

Test-Pattern -Name "Dedicated server launch requires the GNS transport" -Text $serverMain `
    -Pattern 'if\s*\(!isPacketAddressNumericHost\(address\)\).*You cannot use non-numeric addresses for the server\..*std::unique_ptr<GnsTransport>\s+gnsTransport\s*=\s*std::make_unique<GnsTransport>\(GnsMode::Server\);.*gnsTransport->startupServer\(address,\s*static_cast<unsigned\s+short>\(port\),\s*static_cast<unsigned\s+int>\(players\)\);.*Networking\s+networking\(gnsTransport\.get\(\)\);' `
    -Missing $missing

Test-PatternAbsent -Name "Dedicated server launch and master publishing have no RakNet transport fallback" -Text ($serverMain + "`n" + $serverNetworking + "`n" + $serverNetworkingHeader + "`n" + $masterClient + "`n" + $masterClientHeader) `
    -Pattern 'TES3MP_USE_GNS|RakPeerInterface::GetInstance|RakPeerInterface::DestroyInstance|RakNet::RakPeerInterface\s*\*|#include\s+<RakPeer(?:Interface)?\.h>|#include\s+<RakNetTypes\.h>|RakNet::NonNumericHostString|SetIncomingPassword|peer->Startup|SetMaximumIncomingConnections|CRABNET_STARTED|masterPort\s*=\s*25561|peer->Connect\(masterServer|TES3MP_MASTERSERVER_PASSW' `
    -Missing $missing

Test-PatternAbsent -Name "Script function registry does not expose direct RakNet headers" -Text $serverScriptFunctionsHeader `
    -Pattern '#include\s+<RakNetTypes\.h>' `
    -Missing $missing

Test-Pattern -Name "Script ABI network-id type is routed through PacketIdentity" -Text $serverScriptTypesHeader `
    -Pattern '#include\s+<components/openmw-mp/Transport/PacketIdentity\.hpp>.*TypeChar<mwmp::PacketNetworkId\*\*,\s*sizeof\(mwmp::PacketNetworkId\*\*\)>.*enum\s*\{\s*value\s*=\s*''n''\s*\}.*CharType<''n''>.*typedef\s+mwmp::PacketNetworkId\*\*\s+type;' `
    -Missing $missing

Test-PatternAbsent -Name "Script ABI type registry does not expose direct RakNet headers or namespaces" -Text $serverScriptTypesHeader `
    -Pattern '#include\s+<RakNetTypes\.h>|RakNet::' `
    -Missing $missing

Test-PatternAbsent -Name "Master publisher does not unwrap received packets back to RakNet packets" -Text ($masterClient + "`n" + $masterClientHeader) `
    -Pattern 'legacyPacket\(\)|Process\(RakNet::Packet|ProcessPacket\(RakNet::Packet|RakNet::Packet\s*\*' `
    -Missing $missing

Test-Pattern -Name "OpenMW client networking uses only the GNS live transport path" -Text ($clientNetworkingHeader + "`n" + $clientNetworking) `
    -Pattern 'std::unique_ptr<GnsTransport>\s+transport;.*Networking::Networking\(\)\s*\{.*transport\s*=\s*std::make_unique<GnsTransport>\(GnsMode::Client\);.*BasePacket::SetPacketTransport\(transport\.get\(\)\);.*for\s*\(ReceivedPacket\*\s+receivedPacket\s*=\s*transport->receive\(\);\s*receivedPacket;\s*transport->deallocatePacket\(receivedPacket\),\s*receivedPacket\s*=\s*transport->receive\(\)\).*switch\s*\(receivedPacket->id\(\)\).*receiveMessage\(receivedPacket\);.*transport->connect\(ip,\s*port\);.*BaseClientPacketProcessor::SetServerAddr\(incomingPacket->address\(\)\);.*getLocalPlayer\(\)->guid\s*=\s*getLocalSystem\(\)->guid\s*=\s*transport->getMyGuid\(\);.*PacketPreInit\s+packetPreInit;.*ReceivedPacket\*\s+receivedPacket\s*=\s*transport->receive\(\);.*PacketStream\s+bsIn\(receivedPacket->data\(\),\s*receivedPacket->length\(\)\);.*transport->deallocatePacket\(receivedPacket\);.*void\s+Networking::receiveMessage\(ReceivedPacket\*\s+packet\).*packet->length\(\).*packet->id\(\).*SystemProcessor::Process\(\*packet\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW client networking identity state uses PacketIdentity aliases" -Text ($clientNetworkingHeader + "`n" + $clientNetworking + "`n" + $clientBaseClientPacketProcessorHeader + "`n" + $clientBaseClientPacketProcessor) `
    -Pattern '(?=.*PacketAddress\s+serverAddress\(\))(?=.*PacketAddress\s+serverAddr;)(?=.*static\s+void\s+SetServerAddr\(PacketAddress\s+addr\))(?=.*static\s+PacketGuid\s+guid,\s*myGuid;)(?=.*static\s+PacketAddress\s+serverAddr;)(?=.*PacketGuid\s+BaseClientPacketProcessor::guid;)(?=.*PacketGuid\s+BaseClientPacketProcessor::myGuid;)(?=.*PacketAddress\s+BaseClientPacketProcessor::serverAddr;)(?=.*PacketGuid\s+guid\s*=\s*getLocalPlayer\(\)->guid;)(?=.*packetAddressToString\(serverAddr,\s*true\)\.c_str\(\))' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW client networking identity state hides direct RakNet identity types" -Text ($clientNetworkingHeader + "`n" + $clientNetworking + "`n" + $clientBaseClientPacketProcessorHeader + "`n" + $clientBaseClientPacketProcessor) `
    -Pattern 'RakNet::RakNetGUID|RakNet::SystemAddress|#include\s+<RakNetTypes\.h>|serverAddr\.ToString' `
    -Missing $missing

Test-Pattern -Name "OpenMW client packet processors consume received packet wrappers" -Text ($clientSystemProcessorHeader + "`n" + $clientSystemProcessor + "`n" + $clientPlayerProcessorHeader + "`n" + $clientPlayerProcessor + "`n" + $clientActorProcessorHeader + "`n" + $clientActorProcessor + "`n" + $clientObjectProcessorHeader + "`n" + $clientObjectProcessor + "`n" + $clientWorldstateProcessorHeader + "`n" + $clientWorldstateProcessor) `
    -Pattern 'static\s+bool\s+Process\(ReceivedPacket&\s+packet\).*bool\s+SystemProcessor::Process\(ReceivedPacket&\s+packet\).*packet\.data\(\).*getSystemPacket\(packet\.id\(\)\).*packet\.length\(\)\s*==\s*myPacket->headerSize\(\);.*bool\s+PlayerProcessor::Process\(ReceivedPacket&\s+packet\).*getPlayerPacket\(packet\.id\(\)\).*bool\s+ActorProcessor::Process\(ReceivedPacket&\s+packet,\s*ActorList\s*&\s*actorList\).*getActorPacket\(packet\.id\(\)\).*bool\s+ObjectProcessor::Process\(ReceivedPacket&\s+packet,\s*ObjectList\s*&\s*objectList\).*getObjectPacket\(packet\.id\(\)\).*bool\s+WorldstateProcessor::Process\(ReceivedPacket&\s+packet,\s*Worldstate\s*&\s*worldstate\).*getWorldstatePacket\(packet\.id\(\)\)' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW client receive path does not unwrap received packets to RakNet packets" -Text ($clientNetworkingHeader + "`n" + $clientNetworking + "`n" + $clientSystemProcessorHeader + "`n" + $clientSystemProcessor + "`n" + $clientPlayerProcessorHeader + "`n" + $clientPlayerProcessor + "`n" + $clientActorProcessorHeader + "`n" + $clientActorProcessor + "`n" + $clientObjectProcessorHeader + "`n" + $clientObjectProcessor + "`n" + $clientWorldstateProcessorHeader + "`n" + $clientWorldstateProcessor) `
    -Pattern 'legacyPacket\(\)|receiveMessage\(RakNet::Packet|Process\(RakNet::Packet|RakNet::Packet\s*[\*&]' `
    -Missing $missing

Test-PatternAbsent -Name "OpenMW client networking has no RakNet live-transport fallback" -Text ($clientNetworkingHeader + "`n" + $clientNetworking) `
    -Pattern 'TES3MP_USE_GNS|RakPeerInterface::GetInstance|SocketDescriptor|CRABNET_STARTED|peer->Startup|peer->Connect|peer->Receive|peer->DeallocatePacket|peer->Shutdown|DestroyInstance|GetMyGUID' `
    -Missing $missing

Test-PatternAbsent -Name "GNS client/server polling loops have no RakSleep dependency" -Text ($clientNetworking + "`n" + $serverNetworking + "`n" + $masterClient + "`n" + $gnsTransportTests) `
    -Pattern '#include\s+<RakSleep\.h>|RakSleep\s*\(' `
    -Missing $missing

Test-Pattern -Name "Dedicated server console polling uses a local platform helper" -Text ($serverCMake + "`n" + $serverConsoleInputHeader + "`n" + $serverConsoleInput + "`n" + $serverNetworking) `
    -Pattern 'ConsoleInput\.cpp.*ConsoleInput\.hpp.*#include\s+"ConsoleInput\.hpp".*bool\s+hasInput\(\).*_kbhit\(\)\s*!=\s*0.*select\(STDIN_FILENO\s*\+\s*1,\s*&readSet,\s*nullptr,\s*nullptr,\s*&timeout\).*int\s+readChar\(\).*_getch\(\).*read\(STDIN_FILENO,\s*&character,\s*1\).*bool\s+consumeEnterPress\(\).*ConsoleInput::consumeEnterPress\(\)' `
    -Missing $missing

Test-PatternAbsent -Name "Dedicated server console input has no CrabNet Kbhit or Gets helper dependency" -Text ($serverNetworking + "`n" + $serverConsoleInputHeader + "`n" + $serverConsoleInput + "`n" + $serverHandleInput) `
    -Pattern '#include\s+<(?:Kbhit|Gets)|(?<!_)kbhit\s*\(|(?<!_)getch\s*\(|\bGets\b|Kbhit\.h|Gets\.h' `
    -Missing $missing

Test-Pattern -Name "Accepted pre-init replies with empty checksum list, creates player, and requests system handshake" -Text $serverNetworking `
    -Pattern 'else\s*\{.*Client\s+was\s+allowed\s+to\s+connect.*PacketPreInit::PluginContainer\s+tmp;.*packetPreInit\.setChecksums\(&tmp\);.*packetPreInit\.setProtocolVersionInfo\(expectedVersion,\s*expectedProtocolVersion,\s*expectedCommitHash\);.*packetPreInit\.Send\(packet->address\(\)\);.*Players::newPlayer\(packet->guid\(\)\);.*GetPacket\(ID_SYSTEM_HANDSHAKE\)->RequestData\(packet->guid\(\)\);.*return\s+true;' `
    -Missing $missing

Test-Pattern -Name "Packet tests pin pre-init protocol/checksum round-trip and empty accepted response semantics" -Text $basePacketTests `
    -Pattern 'TEST\(MpBasePacketTest,\s*preInitRoundTripsProtocolInfoAndChecksums\).*setProtocolVersionInfo\("0\.1\.0",\s*10,\s*"abcdef1234567890"\).*EXPECT_EQ\(packetId,\s*ID_GAME_PREINIT\).*EXPECT_EQ\(reader\.getVersion\(\),\s*"0\.1\.0"\).*EXPECT_EQ\(reader\.getProtocolVersion\(\),\s*10\).*EXPECT_EQ\(reader\.getCommitHash\(\),\s*"abcdef1234567890"\).*EXPECT_EQ\(receivedChecksums\[1\]\.second\[1\],\s*0x211329EF\).*TEST\(MpBasePacketTest,\s*preInitEmptyChecksumResponseKeepsProtocolInfo\).*receivedChecksums\{\s*\{\s*"stale\.esm".*EXPECT_TRUE\(receivedChecksums\.empty\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Packet tests pin malformed pre-init metadata rejection" -Text $basePacketTests `
    -Pattern 'TEST\(MpBasePacketTest,\s*preInitRejectsMissingChecksumContainer\).*mwmp::PacketPreInit\s+reader;.*reader\.Packet\(&stream,\s*false\);.*EXPECT_FALSE\(reader\.isPacketValid\(\)\);.*TEST\(MpBasePacketTest,\s*preInitRejectsTooManyPlugins\).*index\s*<\s*1001.*reader\.setChecksums\(&receivedChecksums\);.*EXPECT_FALSE\(reader\.isPacketValid\(\)\);.*TEST\(MpBasePacketTest,\s*preInitRejectsOversizedPluginName\).*std::string\(257,\s*''a''\).*EXPECT_FALSE\(reader\.isPacketValid\(\)\);.*TEST\(MpBasePacketTest,\s*preInitRejectsTooManyHashesForPlugin\).*HashList\s+hashes\(51,\s*0x7B6AF5B9\).*EXPECT_FALSE\(reader\.isPacketValid\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin embedded-guid pre-init exchange and stale gameplay packet close behavior" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*exchangesPreInitPacketWithEmbeddedGuid\).*writePreInit\(clientStream,\s*sentChecksums,\s*client\.getMyGuid\(\)\).*EXPECT_EQ\(receivedServerPacket->guid\(\),\s*client\.getMyGuid\(\)\).*PacketStream\s+serverRead\(receivedServerPacket->data\(\),\s*receivedServerPacket->length\(\)\).*EXPECT_EQ\(serverPreInit\.getVersion\(\),\s*"0\.1\.0"\).*EXPECT_TRUE\(responseChecksums\.empty\(\)\);.*TEST\(GnsTransportTest,\s*wrongFirstGameplayPacketCanBeClosedBeforePreInit\).*writePacketIdAndGuid\(stalePacketStream,\s*ID_PLAYER_BASEINFO,\s*client\.getMyGuid\(\)\).*EXPECT_EQ\(receivedStalePacket->guid\(\),\s*client\.getMyGuid\(\)\).*server->closeConnection\(PacketDestination\(receivedStalePacket->guid\(\)\),\s*true\).*EXPECT_EQ\(server->getPacketAddress\(client\.getMyGuid\(\)\),\s*unassignedPacketAddress\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin rapid connect/disconnect churn and replacement routing" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*handlesRapidClientConnectDisconnectChurnAndReplacementRouting\).*constexpr\s+int\s+waveCount\s*=\s*4;.*constexpr\s+int\s+clientCount\s*=\s*4;.*server->closeConnection\(clientAddresses\[1\],\s*true\);.*server->closeConnection\(clientAddresses\[3\],\s*true\);.*EXPECT_EQ\(server->getPacketAddress\(clientGuids\[1\]\),\s*unassignedPacketAddress\(\)\);.*clients\[replacementIndex\]\s*=\s*std::make_unique<mwmp::GnsTransport>\(mwmp::GnsMode::Client\);.*EXPECT_EQ\(server->getPacketAddress\(clientGuids\[replacementIndex\]\),\s*clientAddresses\[replacementIndex\]\);.*server->send\(guidPayload,\s*sizeof\(guidPayload\),\s*PacketPriority::High,.*PacketDestination\(clientGuids\[1\]\),\s*false\).*server->send\(addressPayload,\s*sizeof\(addressPayload\),\s*PacketPriority::High,.*clientAddresses\[2\],\s*false\).*return\s+std::all_of\(clients\.begin\(\),\s*clients\.end\(\),' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin gameplay churn while master browser refreshes continue" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*sustainsGameplayChurnWhileMasterBrowserRefreshes\).*constexpr\s+int\s+gameplayClientCount\s*=\s*3;.*constexpr\s+int\s+browserClientCount\s*=\s*2;.*constexpr\s+int\s+refreshCount\s*=\s*4;.*auto\s+gameplayServer\s*=\s*startLocalServer\(gameplayClientCount\);.*auto\s+masterServer\s*=\s*startLocalServer\(browserClientCount\);.*ASSERT_EQ\(masterServer->numberOfConnections\(\),\s*browserClientCount\);.*ASSERT_EQ\(gameplayServer->numberOfConnections\(\),\s*gameplayClientCount\);.*ID_MASTER_QUERY.*ID_MASTER_UPDATE.*ASSERT_TRUE\(waitForPacketIds\(\*masterServer,\s*\{\s*ID_MASTER_QUERY,\s*ID_MASTER_UPDATE\s*\}\)\);.*PacketDestination\(\),\s*true\).*ASSERT_TRUE\(waitForPacketIds\(\*browserClient,\s*\{\s*ID_MASTER_QUERY,\s*ID_MASTER_UPDATE\s*\}\)\);.*gameplayServer->closeConnection\(gameplayAddresses\[replacementIndex\],\s*true\);.*EXPECT_EQ\(gameplayServer->getPacketAddress\(replacedGuid\),\s*unassignedPacketAddress\(\)\);.*gameplayClients\[replacementIndex\]\s*=\s*std::make_unique<mwmp::GnsTransport>\(mwmp::GnsMode::Client\);.*EXPECT_EQ\(masterServer->numberOfConnections\(\),\s*browserClientCount\);' `
    -Missing $missing

Test-Pattern -Name "GNS address bans normalize hostnames through the transport resolver" -Text $gnsTransport `
    -Pattern 'void\s+mwmp::GnsTransport::banAddress\(const\s+char\*\s+ipAddress\).*const\s+std::string\s+normalizedAddress\s*=\s*normalizeAddressKey\(ipAddress\);.*mBannedAddresses\.insert\(normalizedAddress\);.*closeBannedConnections\(normalizedAddress\);.*void\s+mwmp::GnsTransport::unbanAddress\(const\s+char\*\s+ipAddress\).*const\s+std::string\s+normalizedAddress\s*=\s*normalizeAddressKey\(ipAddress\);.*mBannedAddresses\.erase\(normalizedAddress\);.*std::string\s+mwmp::GnsTransport::normalizeAddressKey\(const\s+std::string&\s+address\).*return\s+ipAddressKey\(resolveAddress\(address,\s*0\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS address bans match active connections by transport-native IP keys" -Text $gnsTransport `
    -Pattern 'setConnectionAddress\(connection,\s*callback\.m_info\.m_addrRemote\);.*if\s*\(isAddressBanned\(callback\.m_info\.m_addrRemote\)\).*mConnectionIpAddressKeys\[connection\]\s*=\s*ipAddressKey\(address\);.*mConnectionIpAddressKeys\.erase\(connection\);.*bool\s+mwmp::GnsTransport::isAddressBanned\(const\s+SteamNetworkingIPAddr&\s+address\)\s+const.*mBannedAddresses\.find\(ipAddressKey\(address\)\).*for\s*\(const\s+auto&\s+\[connection,\s*address\]\s*:\s*mConnectionIpAddressKeys\)' `
    -Missing $missing

Test-Pattern -Name "GNS transport converts IPv6 Steam addresses into packet addresses without bracketed endpoint syntax" -Text $gnsTransport `
    -Pattern 'PacketAddress\s+mwmp::GnsTransport::toPacketAddress\(const\s+SteamNetworkingIPAddr&\s+address\).*std::string\s+hostText\(host\);.*hostText\.front\(\)\s*==\s*''\[''\s*&&\s*hostText\.back\(\)\s*==\s*''\]''.*hostText\s*=\s*hostText\.substr\(1,\s*hostText\.size\(\)\s*-\s*2\);.*return\s+makePacketAddress\(hostText\.c_str\(\),\s*address\.m_port\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin hostname ban rejection and unban reconnect" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*reportsHostnameBannedAddressRejection\).*server->banAddress\("localhost"\).*connectToServer\(\*server,\s*client\).*ID_CONNECTION_BANNED.*EXPECT_EQ\(server->numberOfConnections\(\),\s*0\);.*TEST\(GnsTransportTest,\s*acceptsConnectionAfterHostnameAddressIsUnbanned\).*server->banAddress\("localhost"\);.*server->unbanAddress\("localhost"\);.*establishConnection\(\*server,\s*client\);.*EXPECT_EQ\(server->numberOfConnections\(\),\s*1\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin active hostname ban disconnect cleanup" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*disconnectsActiveConnectionWhenHostnameAddressIsBanned\).*establishConnection\(\*server,\s*client\);.*server->banAddress\("localhost"\);.*ID_CONNECTION_BANNED.*EXPECT_EQ\(server->numberOfConnections\(\),\s*0\);.*EXPECT_FALSE\(client\.isConnected\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin IPv6 ban rejection, unban reconnect, and active disconnect cleanup" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*reportsIpv6BannedAddressRejectionWhenAvailable\).*startLocalServer\(1,\s*"::1"\).*server->banAddress\("::1"\).*connectToServer\(\*server,\s*client,\s*"::1"\).*ID_CONNECTION_BANNED.*TEST\(GnsTransportTest,\s*acceptsConnectionAfterIpv6AddressIsUnbannedWhenAvailable\).*server->banAddress\("::1"\);.*server->unbanAddress\("::1"\);.*establishConnection\(\*server,\s*client,\s*"::1"\);.*TEST\(GnsTransportTest,\s*disconnectsActiveConnectionWhenIpv6AddressIsBannedWhenAvailable\).*establishConnection\(\*server,\s*client,\s*"::1"\);.*server->banAddress\("::1"\);.*ID_CONNECTION_BANNED.*EXPECT_FALSE\(client\.isConnected\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport tests pin Lua admin kick by player GUID cleanup" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*closesConnectionByGuidForServerKick\).*const\s+PacketGuid\s+clientGuid\s*=\s*client\.getMyGuid\(\);.*sendClientPreInitAndGetAddress\(\*server,\s*client\).*server->closeConnection\(mwmp::PacketDestination\(clientGuid\),\s*true\);.*ID_DISCONNECTION_NOTIFICATION.*EXPECT_EQ\(server->numberOfConnections\(\),\s*0\);.*EXPECT_EQ\(server->getPacketAddress\(clientGuid\),\s*unassignedPacketAddress\(\)\);.*EXPECT_FALSE\(client\.isConnected\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS transport keeps legacy admin connection metrics non-negative and cleaned after close" -Text $gnsTransportTests `
    -Pattern 'TEST\(GnsTransportTest,\s*reportsLegacyAdminConnectionMetrics\).*startLocalServer\(2\).*EXPECT_EQ\(server->numberOfConnections\(\),\s*1\);.*EXPECT_EQ\(server->maxConnections\(\),\s*2\);.*EXPECT_EQ\(server->getPacketAddress\(clientGuid\),\s*clientAddress\);.*EXPECT_GE\(server->averagePing\(mwmp::PacketDestination\(clientGuid\)\),\s*0\);.*EXPECT_GE\(server->averagePing\(clientAddress\),\s*0\);.*server->closeConnection\(mwmp::PacketDestination\(clientGuid\),\s*true\);.*EXPECT_EQ\(server->averagePing\(mwmp::PacketDestination\(clientGuid\)\),\s*0\);.*EXPECT_EQ\(server->getPacketAddress\(clientGuid\),\s*unassignedPacketAddress\(\)\);' `
    -Missing $missing

Test-Pattern -Name "GNS average ping clamps transport status into legacy non-negative Lua semantics" -Text $gnsTransport `
    -Pattern 'int\s+mwmp::GnsTransport::averagePing\(const\s+PacketDestination&\s+destination\)\s+const.*GetConnectionRealTimeStatus\(connection,\s*&status,\s*0,\s*nullptr\).*return\s+std::max\(status\.m_nPing,\s*0\);' `
    -Missing $missing

Test-Pattern -Name "Login/world-entry guard still pins wrong-first-packet rejection before handshake ordering" -Text $loginOrderingGuard `
    -Pattern 'Server\s+closes\s+stale\s+clients.*before\s+player\s+creation.*ID_GAME_PREINIT.*transport->closeConnection' `
    -Missing $missing

Write-Host "TES3MP GNS pre-init rejection sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 96"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
