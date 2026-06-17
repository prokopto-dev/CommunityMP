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

$announcePacketHeader = Get-SourceText "components\openmw-mp\Master\PacketMasterAnnounce.hpp"
$packetIdHeader = Get-SourceText "components\openmw-mp\Transport\PacketId.hpp"
$networkMessagesHeader = Get-SourceText "components\openmw-mp\NetworkMessages.hpp"
$announcePacket = Get-SourceText "components\openmw-mp\Master\PacketMasterAnnounce.cpp"
$queryPacket = Get-SourceText "components\openmw-mp\Master\PacketMasterQuery.cpp"
$updatePacket = Get-SourceText "components\openmw-mp\Master\PacketMasterUpdate.cpp"
$masterData = Get-SourceText "components\openmw-mp\Master\MasterData.hpp"
$proxyPacket = Get-SourceText "components\openmw-mp\Master\ProxyMasterPacket.hpp"
$masterClient = Get-SourceText "apps\openmw-mp\MasterClient.cpp"
$masterClientHeader = Get-SourceText "apps\openmw-mp\MasterClient.hpp"
$masterServerHeader = Get-SourceText "apps\master\MasterServer.hpp"
$masterServer = Get-SourceText "apps\master\MasterServer.cpp"
$restServer = Get-SourceText "apps\master\RestServer.cpp"
$serverTest = Get-SourceText "apps\master\ServerTest.cpp"
$browserMain = Get-SourceText "apps\browser\main.cpp"
$browserTypes = Get-SourceText "apps\browser\Types.hpp"
$browserQueryHelper = Get-SourceText "apps\browser\QueryHelper.cpp"
$browserQueryClientHeader = Get-SourceText "apps\browser\netutils\QueryClient.hpp"
$browserQueryClient = Get-SourceText "apps\browser\netutils\QueryClient.cpp"
$browserUtilsHeader = Get-SourceText "apps\browser\netutils\Utils.hpp"
$browserUtils = Get-SourceText "apps\browser\netutils\Utils.cpp"
$browserCMake = Get-SourceText "apps\browser\CMakeLists.txt"
$browserHttpNetworkHeader = Get-SourceText "apps\browser\netutils\HTTPNetwork.hpp"
$browserHttpNetwork = Get-SourceText "apps\browser\netutils\HTTPNetwork.cpp"
$communityBrowserReadme = Get-SourceText "apps\communitymp-hub\README.md"
$communityBrowserPackage = Get-SourceText "apps\communitymp-hub\package.json"
$communityBrowserApp = Get-SourceText "apps\communitymp-hub\src\App.svelte"
$communityBrowserAdmin = Get-SourceText "apps\communitymp-hub\src\AdminEditor.svelte"
$communityBrowserXmlEditor = Get-SourceText "apps\communitymp-hub\src\XmlDocumentEditor.svelte"
$communityBrowserAdminLib = Get-SourceText "apps\communitymp-hub\src\lib\admin.ts"
$communityBrowserStyles = Get-SourceText "apps\communitymp-hub\src\styles.css"
$communityBrowserMain = Get-SourceText "apps\communitymp-hub\src-tauri\src\main.rs"
$communityBrowserAdminRust = Get-SourceText "apps\communitymp-hub\src-tauri\src\admin_editor.rs"
$communityBrowserCli = Get-SourceText "apps\communitymp-hub\src-tauri\src\cli.rs"
$communityBrowserMaster = Get-SourceText "apps\communitymp-hub\src-tauri\src\master.rs"
$communityBrowserLaunch = Get-SourceText "apps\communitymp-hub\src-tauri\src\launch.rs"
$communityBrowserTauri = Get-SourceText "apps\communitymp-hub\src-tauri\tauri.conf.json"
$communityBrowserCapabilities = Get-SourceText "apps\communitymp-hub\src-tauri\capabilities\default.json"
$clientMain = Get-SourceText "apps\openmw\mwmp\Main.cpp"
$endpointHeader = Get-SourceText "components\openmw-mp\Endpoint.hpp"
$endpointTests = Get-SourceText "apps\components_tests\openmw-mp\endpoint.cpp"
$luaCompatWrapper = Get-SourceText "scripts\test-tes3mp-lua-compat.ps1"
$runtimeSmoke = Get-SourceText "scripts\smoke-tes3mp-runtime.ps1"
$componentTests = Get-SourceText "apps\components_tests\openmw-mp\basepacket.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "Master announce packet serializes the GNS advertised game port before server data" -Text $announcePacket `
    -Pattern 'packetID\s*=\s*ID_MASTER_ANNOUNCE;.*orderChannel\s*=\s*CHANNEL_MASTER;.*reliability\s*=\s*PacketReliability::ReliableOrderedWithAckReceipt;.*advertisedPort\s*=\s*0;.*RW\(func,\s*send\);.*RW\(advertisedPort,\s*send\);.*if\s*\(func\s*==\s*FUNCTION_ANNOUNCE\)\s+ProxyMasterPacket::addServer\(this,\s*\*server,\s*send\);.*void\s+PacketMasterAnnounce::SetAdvertisedPort\(uint16_t\s+port\).*advertisedPort\s*=\s*port;.*uint16_t\s+PacketMasterAnnounce::GetAdvertisedPort\(\)\s+const.*return\s+advertisedPort;' `
    -Missing $missing

Test-PatternAbsent -Name "Master announce packet has no conditional GNS/RakNet advertised-port branch" -Text ($announcePacketHeader + "`n" + $announcePacket) `
    -Pattern 'TES3MP_USE_GNS' `
    -Missing $missing

Test-Pattern -Name "Master and browser packet ids are TES3MP-owned" -Text ($packetIdHeader + "`n" + $networkMessagesHeader + "`n" + $masterData + "`n" + $browserUtils) `
    -Pattern 'ID_CONNECTED_PING\s*=\s*0,.*ID_UNCONNECTED_PING\s*=\s*1,.*ID_CONNECTION_REQUEST_ACCEPTED\s*=\s*16,.*ID_CONNECTION_ATTEMPT_FAILED\s*=\s*17,.*ID_CONNECTION_LOST\s*=\s*22,.*ID_USER_PACKET_ENUM\s*=\s*134.*enum\s+GameMessages\s*:\s*mwmp::PacketId.*enum\s+MASTER_PACKETS\s*:\s*mwmp::PacketId.*ID_MASTER_QUERY\s*=\s*ID_USER_PACKET_ENUM.*PingServer\(const\s+char\s+\*addr,\s*unsigned\s+short\s+port\).*ID_CONNECTION_REQUEST_ACCEPTED' `
    -Missing $missing

Test-PatternAbsent -Name "Master and browser packet id definitions do not include RakNet message identifiers" -Text ($packetIdHeader + "`n" + $networkMessagesHeader + "`n" + $masterData + "`n" + $browserUtils) `
    -Pattern '#include\s+<MessageIdentifiers\.h>' `
    -Missing $missing

Test-Pattern -Name "Master query packet round-trips listed endpoint address, port, and detail payload" -Text $queryPacket `
    -Pattern 'packetID\s*=\s*ID_MASTER_QUERY;.*orderChannel\s*=\s*CHANNEL_MASTER;.*int32_t\s+serversCount\s*=\s*packetCount\(servers->size\(\)\);.*RW\(serversCount,\s*send\);.*addr\s*=\s*packetAddressToString\(serverIt->first,\s*false\);.*port\s*=\s*packetAddressPort\(serverIt->first\);.*RW\(addr,\s*send\);.*RW\(port,\s*send\);.*ProxyMasterPacket::addServer\(this,\s*server,\s*send\);.*servers->insert\(std::pair<PacketAddress,\s*QueryData>\(makePacketAddress\(addr\.c_str\(\),\s*port\),\s*server\)\)' `
    -Missing $missing

Test-Pattern -Name "Master update packet round-trips one endpoint address, port, and detail payload" -Text $updatePacket `
    -Pattern 'packetID\s*=\s*ID_MASTER_UPDATE;.*orderChannel\s*=\s*CHANNEL_MASTER;.*std::string\s+addr\s*=\s*packetAddressToString\(server->first,\s*false\);.*uint16_t\s+port\s*=\s*packetAddressPort\(server->first\);.*RW\(addr,\s*send\);.*RW\(port,\s*send\);.*if\s*\(!send\)\s+server->first\s*=\s*makePacketAddress\(addr\.c_str\(\),\s*port\);.*ProxyMasterPacket::addServer\(this,\s*server->second,\s*send\);' `
    -Missing $missing

Test-Pattern -Name "Master query data keeps TES3MP browser name/version/player/max/gamemode/password rules" -Text $masterData `
    -Pattern 'rules\["name"\]\.type\s*=\s*ServerRule::Type::string;.*rules\["version"\]\.type\s*=\s*ServerRule::Type::string;.*rules\["players"\]\.type\s*=\s*ServerRule::Type::number;.*rules\["maxPlayers"\]\.type\s*=\s*ServerRule::Type::number;.*rules\["gamemode"\]\.type\s*=\s*ServerRule::Type::string;.*rules\["passw"\]\.type\s*=\s*ServerRule::Type::number;' `
    -Missing $missing

Test-Pattern -Name "Master detail payload preserves rules, visible player names, and plugin hashes" -Text $proxyPacket `
    -Pattern 'int32_t\s+rulesSize\s*=\s*packetCount\(server\.rules\.size\(\)\);.*packet->RW\(rulesSize,\s*send\);.*packet->RW\(key,\s*send,\s*false,\s*QueryData::maxStringLength\);.*packet->RW\(rule->type,\s*send\);.*packet->RW\(rule->str,\s*send,\s*QueryData::maxStringLength\).*int32_t\s+playersCount\s*=\s*packetCount\(server\.players\.size\(\)\);.*packet->RW\(playersCount,\s*send\);.*for\(auto\s+&&player\s*:\s*server\.players\)\s+packet->RW\(player,\s*send,\s*false,\s*QueryData::maxStringLength\);.*int32_t\s+pluginsCount\s*=\s*packetCount\(server\.plugins\.size\(\)\);.*packet->RW\(pluginsCount,\s*send\);.*packet->RW\(plugin\.name,\s*send,\s*false,\s*QueryData::maxStringLength\);.*packet->RW\(plugin\.hash,\s*send\);' `
    -Missing $missing

Test-Pattern -Name "Master publisher marks player, hostname, gamemode, rule, and plugin mutations as updated" -Text $masterClient `
    -Pattern 'void\s+MasterClient::SetPlayers\(unsigned\s+pl\).*queryData\.SetPlayers\(players\);.*updated\s*=\s*true;.*void\s+MasterClient::SetHostname\(std::string\s+hostname\).*queryData\.SetName\(substr\.c_str\(\)\);.*updated\s*=\s*true;.*void\s+MasterClient::SetModname\(std::string\s+modname\).*queryData\.SetGameMode\(substr\.c_str\(\)\);.*updated\s*=\s*true;.*void\s+MasterClient::SetRuleString\(std::string\s+key,\s*std::string\s+value\).*queryData\.rules\[key\]\s*=\s*rule;.*updated\s*=\s*true;.*void\s+MasterClient::PushPlugin\(Plugin\s+plugin\).*queryData\.plugins\.push_back\(plugin\);.*updated\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "GNS master publisher connects per update and advertises the gameplay port, not the publisher connection port" -Text $masterClient `
    -Pattern 'bool\s+MasterClient::Process\(ReceivedPacket\*\s+packet\).*bool\s+MasterClient::ProcessPacket\(ReceivedPacket\*\s+packet,\s*bool\s+requireMasterAddress\).*packet->address\(\)\s*!=\s*masterServer.*PacketStream\s+rs\(packet->data\(\),\s*packet->length\(\)\).*masterTransport\s*=\s*std::make_unique<mwmp::GnsTransport>\(mwmp::GnsMode::Client,\s*false\);.*masterTransport->connect\(masterHost,\s*masterPort\);.*ProcessPacket\(receivedPacket,\s*false\).*pma\.SetFunc\(func\);.*pma\.SetAdvertisedPort\(Networking::get\(\)\.getPort\(\)\);.*pma\.Packet\(&writeStream,\s*true\);.*masterTransport->send\(writeStream\.data\(\),\s*writeStream\.size\(\),\s*PacketPriority::High,\s*PacketReliability::ReliableOrderedWithAckReceipt,\s*CHANNEL_MASTER,\s*masterServer,\s*false\)' `
    -Missing $missing

Test-PatternAbsent -Name "Master publisher does not unwrap received packets back to RakNet packets" -Text ($masterClient + "`n" + $masterClientHeader) `
    -Pattern 'legacyPacket\(\)|Process\(RakNet::Packet|ProcessPacket\(RakNet::Packet|RakNet::Packet\s*\*' `
    -Missing $missing

Test-Pattern -Name "Master publisher refreshes visible player names from the TES3MP player snapshot" -Text $masterClient `
    -Pattern 'const\s+auto\s+\[playerCount,\s*playerNames\]\s*=\s*Players::getMasterListSnapshot\(\);.*if\s*\(queryData\.GetPlayers\(\)\s*!=\s*static_cast<int>\(playerCount\)\).*queryData\.SetPlayers\(static_cast<int>\(playerCount\)\);.*updated\s*=\s*true;.*if\s*\(queryData\.players\s*!=\s*playerNames\).*queryData\.players\s*=\s*playerNames;.*updated\s*=\s*true;' `
    -Missing $missing

Test-Pattern -Name "GNS master server stores announced servers under the advertised gameplay endpoint" -Text $masterServer `
    -Pattern 'case\s+ID_MASTER_ANNOUNCE:.*pma\.SetReadStream\(&data\);.*pma\.SetServer\(&server\);.*pma\.Read\(\);.*PacketAddress\s+serverAddress\s*=\s*receivedPacket->address\(\);.*if\s*\(pma\.GetAdvertisedPort\(\)\s*!=\s*0\)\s+setPacketAddressPortHostOrder\(serverAddress,\s*pma\.GetAdvertisedPort\(\)\);.*ServerIter\s+iter\s*=\s*servers\.find\(serverAddress\);.*servers\.insert\(\{serverAddress,\s*server\}\)' `
    -Missing $missing

Test-Pattern -Name "Standalone master server uses only the GNS transport path" -Text $masterServer `
    -Pattern 'transport\s*=\s*std::make_unique<mwmp::GnsTransport>\(mwmp::GnsMode::Server,\s*false\);.*transport->startupServer\("",\s*port,\s*maxConnections\);.*PacketMasterQuery\s+pmq;.*PacketMasterUpdate\s+pmu;.*PacketMasterAnnounce\s+pma;.*transport->send\(send\.data\(\),\s*send\.size\(\),\s*PacketPriority::High,\s*PacketReliability::ReliableOrderedWithAckReceipt,\s*CHANNEL_MASTER,\s*destination,\s*false\).*ReceivedPacket\*\s+receivedPacket\s*=\s*transport->receive\(\).*transport->deallocatePacket\(receivedPacket\).*PacketStream\s+data\(receivedPacket->data\(\),\s*receivedPacket->length\(\)\).*transport->closeConnection\(receivedPacket->destination\(\),\s*true\)' `
    -Missing $missing

Test-PatternAbsent -Name "Standalone master server has no RakNet fallback receive or ACK path" -Text $masterServer `
    -Pattern 'RakPeerInterface|peer->|pendingACKs|SetIncomingPassword|TES3MP_MASTERSERVER_PASSW|ID_SND_RECEIPT_ACKED|pmq\.Send|pmu\.Send|pma\.Send|DestroyInstance' `
    -Missing $missing

Test-Pattern -Name "Master server test utility uses the GNS client transport path" -Text $serverTest `
    -Pattern 'bool\s+waitForConnection\(GnsTransport&\s+transport\).*ReceivedPacket\*\s+receivedPacket\s*=\s*transport\.receive\(\).*switch\s*\(receivedPacket->id\(\)\).*packetGuidToString\(receivedPacket->guid\(\)\)\.c_str\(\).*PacketAddress\s+masterAddr\s*=\s*makePacketAddress\("127\.0\.0\.1",\s*25560\);.*GnsTransport\s+transport\(GnsMode::Client,\s*false\);.*transport\.connect\(packetAddressToString\(masterAddr,\s*false\),\s*packetAddressPort\(masterAddr\)\);.*BasePacket::SetPacketTransport\(&transport\);.*transport\.deallocatePacket\(receivedPacket\).*PacketStream\s+data\(receivedPacket->data\(\),\s*receivedPacket->length\(\)\).*transport\.closeConnection\(masterAddr,\s*true\)' `
    -Missing $missing

Test-PatternAbsent -Name "Master server test utility has no RakPeer or CrabNet console helper path" -Text $serverTest `
    -Pattern 'RakPeerInterface|SocketDescriptor|ConnectionAttemptResult|peer->|DestroyInstance|GetConnectionState|IS_CONNECTED|RakSleep|(?<!_)kbhit|Gets|#include\s+<(?:RakPeer|Kbhit|Gets)' `
    -Missing $missing

Test-Pattern -Name "GNS master server answers list and detail requests from stored endpoint snapshots" -Text $masterServer `
    -Pattern 'auto\s+makeQuerySnapshot\s*=\s*\[this\]\(\).*map<PacketAddress,\s*QueryData>\s+snapshot;.*snapshot\.emplace\(address,\s*static_cast<const\s+QueryData&>\(server\)\);.*case\s+ID_MASTER_QUERY:.*auto\s+serverSnapshot\s*=\s*makeQuerySnapshot\(\);.*pmq\.SetServers\(&serverSnapshot\);.*sendPacket\(pmq,\s*receivedPacket->address\(\)\);.*case\s+ID_MASTER_UPDATE:.*PacketAddress\s+addr;.*readPacketAddress\(data,\s*addr\).*ServerIter\s+it\s*=\s*servers\.find\(addr\);.*pair<PacketAddress,\s*QueryData>\s+pairPtr.*pmu\.SetServer\(&pairPtr\);.*sendPacket\(pmu,\s*receivedPacket->address\(\)\);' `
    -Missing $missing

Test-Pattern -Name "Master server REST and browser list formatting use PacketAddress helpers" -Text ($restServer + "`n" + $browserQueryHelper) `
    -Pattern 'serverMap->at\(makePacketAddress\(addr\.c_str\(\),\s*port\)\).*queryToStringStream\(ss,\s*packetAddressToString\(query->first,\s*true,\s*'':''\),\s*query->second\).*serverMap->insert\(\{makePacketAddress\(request->remote_endpoint_address\.c_str\(\),\s*port\),\s*server\}\).*serverMap->find\(makePacketAddress\(request->remote_endpoint_address\.c_str\(\),\s*port\)\).*emit\s+updateModel\(QString::fromStdString\(mwmp::packetAddressToString\(server\.first,\s*false\)\),\s*mwmp::packetAddressPort\(server\.first\),' `
    -Missing $missing

Test-PatternAbsent -Name "Master server and browser endpoint storage does not expose direct SystemAddress APIs" -Text ($masterServerHeader + "`n" + $masterServer + "`n" + $restServer + "`n" + $browserQueryHelper + "`n" + $serverTest) `
    -Pattern 'RakNet::SystemAddress|\bSystemAddress\b|UNASSIGNED_SYSTEM_ADDRESS|\.SetPortHostOrder|address\(\)\.ToString|serverAddress\.ToString|serverPair\.first\.ToString|server\.first\.ToString|query->first\.ToString|masterAddr\.ToString|masterAddr\.GetPort|server\.first\.GetPort|query->first\.GetPort|guid\(\)\.g|guid\(\)\.ToString' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub CMake target builds the Rust and Svelte executable" -Text $browserCMake `
    -Pattern 'if\(WIN32\).*find_program\(COMMUNITYMP_NPM_EXECUTABLE\s+NAMES\s+npm\.cmd\s+REQUIRED\).*else\(\).*find_program\(COMMUNITYMP_NPM_EXECUTABLE\s+NAMES\s+npm\s+REQUIRED\).*add_custom_target\(communitymp-hub\s+ALL.*\$\{COMMUNITYMP_NPM_EXECUTABLE\}\s+ci.*CARGO_TARGET_DIR=\$\{COMMUNITYMP_HUB_CARGO_TARGET_DIR\}.*\$\{COMMUNITYMP_NPM_EXECUTABLE\}\s+run\s+build.*copy_if_different.*communitymp-hub' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub CLI has one-shot master query, server detail, and direct ping probes" -Text ($communityBrowserCli + "`n" + $communityBrowserMaster) `
    -Pattern '--query-master-once.*--query-server-once.*--ping-server-once.*query_servers_impl\(master_host,\s*master_port,\s*rest_port\).*Master query returned.*query_server_details_impl\(master_host,\s*rest_port,\s*address\).*Master update details: listedPlayers=0.*Server ping returned' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub one-shot probes accept explicit master, REST, and server endpoints" -Text $communityBrowserCli `
    -Pattern '--master-address=.*--master-port=.*--master-rest-port=.*--server-address=.*--server-port=' `
    -Missing $missing

Test-Pattern -Name "Endpoint parser preserves default ports, invalid-port fallback, and IPv6 literal formatting" -Text $endpointHeader `
    -Pattern 'constexpr\s+unsigned\s+short\s+defaultTes3mpPort\s*=\s*25565;.*parseEndpointPort\(std::string_view\s+value\).*port\s*==\s*0.*parseServerEndpoint\(std::string_view\s+endpoint,\s*unsigned\s+short\s+defaultPort\s*=\s*defaultTes3mpPort\).*closingBracket.*parseEndpointPort\(std::string_view\(text\)\.substr\(closingBracket\s*\+\s*2\)\).*firstColon\s*!=\s*std::string::npos\s*&&\s*firstColon\s*==\s*lastColon.*text\.substr\(0,\s*lastColon\),\s*defaultPort.*formatServerEndpoint\(std::string_view\s+host,\s*unsigned\s+short\s+port\).*hostText\.find' `
    -Missing $missing

Test-Pattern -Name "Direct client launch and browser favorites consume the shared endpoint parser" -Text ($clientMain + "`n" + $browserTypes) `
    -Pattern 'parseServerEndpoint\(address\).*splitServerAddress\(QString\s+addr\).*mwmp::parseServerEndpoint\(addr\.toStdString\(\)\).*formatServerAddress\(const\s+AddrPair&\s+addr\).*mwmp::formatServerEndpoint\(addr\.first\.toStdString\(\),\s*addr\.second\)' `
    -Missing $missing

Test-Pattern -Name "Component and wrapper coverage pin endpoint parser edge cases" -Text ($endpointTests + "`n" + $luaCompatWrapper) `
    -Pattern 'invalidOrZeroPortFallsBackToDefaultPort.*bareIpv6AddressUsesDefaultPort.*bracketedIpv6AddressUsesExplicitPort.*bracketedIpv6WithInvalidPortFallsBackToDefaultPort.*formatBracketsIpv6Address.*Tes3mpEndpointTest\.\*' `
    -Missing $missing

Test-Pattern -Name "GNS browser query client sends query and update packets on the master channel" -Text $browserQueryClient `
    -Pattern 'map<PacketAddress,\s*QueryData>\s+QueryClient::Query\(\).*ID_MASTER_QUERY.*transport->send\(bs\.data\(\),\s*bs\.size\(\),\s*PacketPriority::High,\s*PacketReliability::ReliableOrderedWithAckReceipt,\s*CHANNEL_MASTER,\s*masterAddr,\s*false\).*pmq->SetServers\(&query\);.*GetAnswer\(ID_MASTER_QUERY\);.*pair<PacketAddress,\s*QueryData>\s+QueryClient::Update\(const\s+PacketAddress\s+&addr\).*ID_MASTER_UPDATE.*writePacketAddress\(bs,\s*addr\);.*pmu->SetServer\(&serverInfo\);.*transport->send\(bs\.data\(\),\s*bs\.size\(\),\s*PacketPriority::High,\s*PacketReliability::ReliableOrderedWithAckReceipt,\s*CHANNEL_MASTER,\s*masterAddr,\s*false\).*GetAnswer\(ID_MASTER_UPDATE\);' `
    -Missing $missing

Test-PatternAbsent -Name "Master and browser endpoint APIs do not expose RakNet SystemAddress" -Text ($browserQueryClientHeader + "`n" + $browserQueryClient + "`n" + $queryPacket + "`n" + $updatePacket) `
    -Pattern 'RakNet::SystemAddress|std::map<SystemAddress,\s*QueryData>|std::pair<SystemAddress,\s*QueryData>|QueryClient::Update\(const\s+RakNet::SystemAddress|SetServers\(std::map<RakNet::SystemAddress|SetServer\(std::pair<RakNet::SystemAddress' `
    -Missing $missing

Test-Pattern -Name "Browser query client uses a GNS-only connection lifecycle" -Text ($browserQueryClientHeader + "`n" + $browserQueryClient) `
    -Pattern 'bool\s+Connect\(\);.*std::unique_ptr<mwmp::GnsTransport>\s+transport;.*std::string\s+masterHost;.*pmq\s*=\s*new\s+PacketMasterQuery\(\);.*pmu\s*=\s*new\s+PacketMasterUpdate\(\);.*transport->closeConnection\(masterAddr,\s*true\);.*ReceivedPacket\*\s+receivedPacket\s*=\s*transport->receive\(\).*transport->deallocatePacket\(receivedPacket\).*transport\s*=\s*std::make_unique<mwmp::GnsTransport>\(mwmp::GnsMode::Client,\s*false\);.*transport->connect\(masterHost,\s*masterPort\);' `
    -Missing $missing

Test-PatternAbsent -Name "Browser query client has no RakNet master-query fallback" -Text ($browserQueryClientHeader + "`n" + $browserQueryClient) `
    -Pattern 'TES3MP_USE_GNS|RakPeerInterface|peer->|SocketDescriptor|DestroyInstance|TES3MP_MASTERSERVER_PASSW|ConnectionAttemptResult|IS_NOT_CONNECTED|IS_CONNECTED|GetConnectionState|PacketReliability::ReliableOrdered,\s*CHANNEL_MASTER' `
    -Missing $missing

Test-Pattern -Name "Legacy Qt browser direct ping path remains GNS-only until Rust ping parity lands" -Text ($browserUtilsHeader + "`n" + $browserUtils + "`n" + $browserMain) `
    -Pattern 'unsigned\s+int\s+PingServer\(const\s+char\s+\*addr,\s*unsigned\s+short\s+port\);.*unsigned\s+int\s+PingServer\(const\s+char\s+\*addr,\s*unsigned\s+short\s+port\).*mwmp::GnsTransport\s+transport\(mwmp::GnsMode::Client,\s*false\);.*transport\.connect\(addr,\s*port\);.*transport\.receive\(\).*ID_CONNECTION_REQUEST_ACCEPTED.*PING_UNREACHABLE.*PingServer\(serverAddr\.c_str\(\),\s*static_cast<unsigned\s+short>\(serverPort\)\)' `
    -Missing $missing

Test-PatternAbsent -Name "Browser direct ping has no RakNet fallback or stale extended-data probe" -Text ($browserUtilsHeader + "`n" + $browserUtils + "`n" + $browserMain) `
    -Pattern 'TES3MP_USE_GNS|PingRakNetServer|getExtendedData|ServerExtendedData|RakPeerInterface|SocketDescriptor|peer->|DestroyInstance|GetTimeMS|UNCONNECTED_PONG|ID_CONNECTED_PING|TES3MP_PROTO_VERSION|ID_USER_PACKET_ENUM|PacketReliability::ReliableOrdered,\s*0' `
    -Missing $missing

Test-PatternAbsent -Name "Master and browser polling loops have no RakSleep dependency" -Text ($masterClient + "`n" + $masterServer + "`n" + $serverTest + "`n" + $browserQueryClient + "`n" + $browserUtils + "`n" + $browserHttpNetwork) `
    -Pattern '#include\s+<RakSleep\.h>|RakSleep\s*\(' `
    -Missing $missing

Test-Pattern -Name "Legacy browser HTTP helper uses Qt Network instead of RakNet TCP plugins" -Text ($browserHttpNetworkHeader + "`n" + $browserHttpNetwork) `
    -Pattern 'QNetworkAccessManager.*QNetworkRequest.*QNetworkReply.*QEventLoop.*HTTPNetwork::getData.*manager\.get.*HTTPNetwork::getDataPOST.*manager\.post.*HTTPNetwork::getDataPUT.*manager\.put' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub Rust and Svelte scaffold pins product version and public endpoints" -Text ($communityBrowserReadme + "`n" + $communityBrowserPackage + "`n" + $communityBrowserApp + "`n" + $communityBrowserTauri + "`n" + $communityBrowserMaster) `
    -Pattern '(?=.*CommunityMP Hub)(?=.*0\.1\.0)(?=.*master\.communitymp\.com)(?=.*https://communitymp\.com)(?=.*svelte-check)(?=.*tauri build --no-bundle)(?=.*mainBinaryName"\s*:\s*"communitymp-hub)(?=.*com\.communitymp\.hub)(?=.*tauri::command)(?=.*default_master_endpoint)(?=.*query_servers)(?=.*query_server_details)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub rewrite pins REST master list, local favorites, and safe account-name launch handoff" -Text ($communityBrowserReadme + "`n" + $communityBrowserMaster + "`n" + $communityBrowserLaunch + "`n" + $communityBrowserApp) `
    -Pattern '(?=.*REST list from `/api/servers`)(?=.*fall back to the list)(?=.*favorites)(?=.*recent servers)(?=.*safe launch-argument previews)(?=.*reqwest::Client::builder)(?=.*api/servers)(?=.*"list servers")(?=.*parse_server_summary)(?=.*query_server_details_from_list)(?=.*build_launch_plan)(?=.*GAME_EXECUTABLE:\s*&str\s*=\s*"communitymp")(?=.*"--client")(?=.*"--connect=\{address\}")(?=.*"--name=\{account_name\}")(?=.*"--password=<server-password>")(?=.*Account passwords are never included)(?=.*localStorage\.setItem\(favoritesKey)(?=.*localStorage\.setItem\(historyKey)(?=.*Prepare Launch)' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub pins frameless adaptive Morrowind shell and mod manager launch compatibility" -Text ($communityBrowserReadme + "`n" + $communityBrowserApp + "`n" + $communityBrowserStyles + "`n" + $communityBrowserLaunch + "`n" + $communityBrowserTauri + "`n" + $communityBrowserCapabilities + "`n" + $communityBrowserMain) `
    -Pattern '(?=.*MysticCards\.ttf)(?=.*Morrowind-styled)(?=.*decorations"\s*:\s*false)(?=.*useHttpsScheme"\s*:\s*true)(?=.*windows_subsystem\s*=\s*"windows")(?=.*"label"\s*:\s*"main")(?=.*core:default)(?=.*core:window:allow-close)(?=.*core:window:allow-minimize)(?=.*core:window:allow-toggle-maximize)(?=.*core:window:allow-start-dragging)(?=.*-webkit-app-region:\s*drag)(?=.*-webkit-app-region:\s*no-drag)(?=.*currentMonitor)(?=.*LogicalSize)(?=.*setSizeConstraints)(?=.*fitWindowToMonitor)(?=.*getCurrentWindow)(?=.*startWindowDrag)(?=.*window-controls)(?=.*stopPropagation)(?=.*contextmenu)(?=.*ResizeObserver)(?=.*applyAdaptiveLayout)(?=.*harmonicMean)(?=.*easeOutCubic)(?=.*--hub-frame-padding)(?=.*--hub-sidebar-width)(?=.*--hub-tab-columns)(?=.*--hub-admin-metric-columns)(?=.*migrateLocalStateKeys)(?=.*communitymp\.browser\.favorites)(?=.*app-bottom-safe-area)(?=.*scroll-padding-bottom)(?=.*shell\.maximized)(?=.*Mod Setup)(?=.*modDataDirectories)(?=.*modContentFiles)(?=.*dataDirectories)(?=.*contentFiles)(?=.*"--data=\{data\}")(?=.*"--content=\{content\}")' `
    -Missing $missing

Test-Pattern -Name "CommunityMP Hub includes MMO admin save editor workspace" -Text ($communityBrowserApp + "`n" + $communityBrowserAdmin + "`n" + $communityBrowserXmlEditor + "`n" + $communityBrowserAdminLib + "`n" + $communityBrowserAdminRust + "`n" + $communityBrowserStyles + "`n" + $communityBrowserMain) `
    -Pattern '(?=.*AdminEditor)(?=.*XmlDocumentEditor)(?=.*type\s+HubTab\s*=\s*"all"\s*\|\s*"favorites"\s*\|\s*"history"\s*\|\s*"mods"\s*\|\s*"admin")(?=.*Admin Editor)(?=.*load_admin_workspace)(?=.*read_admin_document)(?=.*save_admin_document)(?=.*save_admin_structured_document)(?=.*save_admin_state)(?=.*StructuredDocumentSaveRequest)(?=.*structured_document_save_rebuilds_editable_tree_and_backup)(?=.*BANLIST_RELATIVE_PATH\s*:\s*&str\s*=\s*"saves/server/security/banlist\.xml")(?=.*DATA_FILES_RELATIVE_PATH\s*:\s*&str\s*=\s*"saves/server/config/data-files\.xml")(?=.*WORLD_MANIFEST_RELATIVE_PATH\s*:\s*&str\s*=\s*"saves/world/manifest\.xml")(?=.*fn\s+write_relative_file)(?=.*backup_path)(?=.*function\s+saveStructuredChanges)(?=.*function\s+saveStructuredDocument)(?=.*function\s+openDocument)(?=.*function\s+saveSourceDocument)(?=.*Blocked Accounts)(?=.*Required Data Files)(?=.*Accounts And Characters)(?=.*World State)(?=.*Documents)(?=.*Raw XML)(?=.*Fields)(?=.*document-workspace)(?=.*admin-workspace-body)(?=.*xml-node-grid)(?=.*xml-attribute-grid)(?=.*admin-tip)(?=.*admin-slider)(?=.*admin-table)(?=.*loadAdminWorkspace)(?=.*saveAdminState)(?=.*saveAdminDocument)(?=.*saveAdminStructuredDocument)' `
    -Missing $missing

Test-PatternAbsent -Name "Browser HTTP helper has no RakNet TCP or HTTP plugin dependency" -Text ($browserHttpNetworkHeader + "`n" + $browserHttpNetwork) `
    -Pattern 'RakPeer|HTTPConnection2|TCPInterface|RakString|FormatForGET|FormatForPOST|FormatForPUT|TransmitRequest|GetResponse' `
    -Missing $missing

Test-Pattern -Name "Browser query client parses master query and update replies into packet helpers" -Text $browserQueryClient `
    -Pattern 'pmq->SetReadStream\(&data\);.*pmu->SetReadStream\(&data\);.*case\s+ID_MASTER_QUERY:.*if\s*\(waitingPacket\s*==\s*ID_MASTER_QUERY\)\s+pmq->Read\(\);.*id\s*=\s*pid;.*case\s+ID_MASTER_UPDATE:.*if\s*\(waitingPacket\s*==\s*ID_MASTER_UPDATE\)\s+pmu->Read\(\);.*id\s*=\s*pid;' `
    -Missing $missing

Test-Pattern -Name "Runtime smoke verifies local master announcement, REST listing, repeated Hub query, detail update, and direct ping refreshes" -Text $runtimeSmoke `
    -Pattern 'Alias\("BrowserProbeIterations"\).*?\[int\]\$HubProbeIterations\s*=\s*3.*if\s*\(\$WithLocalMaster\).*HubProbeIterations must be at least 1.*Added server\|Updated server\|Keeping alive server.*Invoke-WebRequest\s+-UseBasicParsing\s+-Uri\s+"http://127\.0\.0\.1:8080/api/servers".*127\\\.0\\\.0\\\.1:25565.*for\s*\(\$probeIndex\s*=\s*1;\s*\$probeIndex\s*-le\s*\$HubProbeIterations;\s*\+\+\$probeIndex\).*"--query-master-once",\s*"--master-address=localhost",\s*"--master-port=25560".*Hub master query smoke #\$probeIndex.*"--query-server-once",\s*"--master-address=localhost",\s*"--master-port=25560",\s*"--server-address=127\.0\.0\.1",\s*"--server-port=25565".*Hub master update smoke #\$probeIndex.*Master update details: listedPlayers=0 reportedPlayers=0 maxPlayers=64 plugins=\d+ rules=\[1-9\]\[0-9\]\*.*"--ping-server-once",\s*"--server-address=127\.0\.0\.1",\s*"--server-port=25565".*Hub direct ping smoke #\$probeIndex.*Server ping returned' `
    -Missing $missing

Test-Pattern -Name "Component coverage pins master announce advertised port round-trip" -Text $componentTests `
    -Pattern 'TEST\(MpBasePacketTest,\s*masterAnnounceRoundTripsServerDataAndAdvertisedPort\).*writer\.SetFunc\(mwmp::PacketMasterAnnounce::FUNCTION_ANNOUNCE\);.*writer\.SetAdvertisedPort\(25565\);.*EXPECT_EQ\(packetId,\s*ID_MASTER_ANNOUNCE\);.*EXPECT_EQ\(reader\.GetFunc\(\),\s*mwmp::PacketMasterAnnounce::FUNCTION_ANNOUNCE\);.*EXPECT_EQ\(reader\.GetAdvertisedPort\(\),\s*25565\);.*expectMasterServerData\(received\);' `
    -Missing $missing

Test-Pattern -Name "Component coverage pins master query list payload round-trip" -Text $componentTests `
    -Pattern 'TEST\(MpBasePacketTest,\s*masterQueryRoundTripsListedServersWithDetailPayloads\).*sentServers\.emplace\(firstAddress,\s*makeMasterServerData\("CommunityMP"\)\);.*sentServers\.emplace\(secondAddress,\s*makeMasterServerData\("TES3MP Remote"\)\);.*EXPECT_EQ\(packetId,\s*ID_MASTER_QUERY\);.*ASSERT_TRUE\(receivedServers\.contains\(firstAddress\)\);.*ASSERT_TRUE\(receivedServers\.contains\(secondAddress\)\);.*expectMasterServerData\(receivedServers\.at\(firstAddress\),\s*"CommunityMP"\);.*expectMasterServerData\(receivedServers\.at\(secondAddress\),\s*"TES3MP Remote"\);' `
    -Missing $missing

Test-Pattern -Name "Component coverage pins master update detail payload round-trip" -Text $componentTests `
    -Pattern 'TEST\(MpBasePacketTest,\s*masterUpdateRoundTripsSingleServerDetails\).*std::pair<mwmp::PacketAddress,\s*QueryData>\s+sent\(sentAddress,\s*makeMasterServerData\("TES3MP Details"\)\);.*EXPECT_EQ\(packetId,\s*ID_MASTER_UPDATE\);.*EXPECT_EQ\(mwmp::packetAddressToString\(received\.first,\s*false\),\s*mwmp::packetAddressToString\(sentAddress,\s*false\)\);.*EXPECT_EQ\(mwmp::packetAddressPort\(received\.first\),\s*mwmp::packetAddressPort\(sentAddress\)\);.*expectMasterServerData\(received\.second,\s*"TES3MP Details"\);' `
    -Missing $missing

Write-Host "TES3MP master/browser sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: 42"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
