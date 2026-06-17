# CommunityMP runtime unification notes

CommunityMP is moving from separate TES3MP/OpenMW-era executables toward a
single runtime entry point that can run client or server mode.

## Current state

- `communitymp.exe --server` runs the dedicated server in-process by calling
  `runCommunityMpDedicatedServer`.
- `communitymp.exe --client` runs the OpenMW/CommunityMP client in-process by
  calling `runApplication`.
- `communitymp.exe --client --external-client` is a temporary fallback that
  resolves `communitymp-client.exe` and starts the old client child process.
- `communitymp.exe --server --external-server` is a temporary fallback that
  resolves `communitymp-server.exe` and starts the old server child process.
- `communitymp-client.exe` is now a thin standalone wrapper around
  `runApplication`.
- `communitymp-server.exe` is now a thin standalone wrapper around
  `runCommunityMpDedicatedServer`.
- `openmw-client-core` is the reusable CMake static library containing the
  OpenMW/CommunityMP client implementation.
- `tes3mp-server-core` is the reusable CMake static library containing the
  dedicated server implementation and packet processors.
- The dedicated-server networking class is now `mwmp::ServerNetworking`, which
  prevents the first client/server symbol collision with client-side
  `mwmp::Networking` when both cores are linked into `communitymp.exe`.

This split is intentional. It keeps the standalone server wrapper available
while the default client and server modes move into the single
`communitymp.exe` runtime.

## Next runtime slice

The server path and client path now both have tested conversion layers from
Windows wide command-line arguments to stable UTF-8 `char*` storage for the
lifetime of the call. Client mode calls `runApplication` directly instead of
nesting `Debug::wrapApplication`; the outer executable already owns the process
identity, and nesting the Windows crash catcher caused `--client --version` to
hang with monitor processes still alive.

Keep `--external-client` and `--external-server` until packaged and live-host
smoke tests prove the in-process paths across gameplay launch, restart, crash,
console input, master-list update, and multi-client test flows.

The remaining executable-unification work is mostly ownership cleanup:

- audit any remaining duplicate client/server `mwmp` class names that are not
  pulled by the current link but could collide as more code moves in-process;
- decide whether crash catching belongs in `communitymp.exe` as a mode-aware
  outer wrapper instead of in each standalone child wrapper;
- retire child executables from release packages only after the fallback paths
  are no longer needed for diagnosis.

## Lua migration boundary

The new `ID_PLAYER_LUA_EVENT` path is the first client-LuaJIT-to-server bridge.
It is deliberately packet-versioned and size-capped, and the server receives it
in C++ without routing it through the legacy server Lua callback surface.

Until server-side OpenMW world simulation is real, keep server-authoritative
actors disabled. NPC AI snapshots should continue to come from the active cell
authority client and be validated/coalesced by C++ server services. Moving AI
fully server-side requires a headless OpenMW simulation layer with pathgrid,
scripts, mechanics, combat, and cell activation semantics.
