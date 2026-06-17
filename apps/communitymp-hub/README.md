# CommunityMP Hub

This is the desktop hub for CommunityMP. The normal CMake target is `communitymp-hub`, and the runtime executable is `communitymp-hub.exe`.

## Stack

- Rust command backend through Tauri.
- TypeScript and Svelte frontend.
- CommunityMP master default: `master.communitymp.com:25560`.
- Website: `https://communitymp.com`.
- Embedded server-host admin editor for CommunityMP XML save, config, player, and world inspection.

## Current Scope

- Keep product/version/domain values aligned with CommunityMP `0.1.0`.
- Keep master query work behind a narrow Rust command boundary. The current implementation can consume the master server REST list from `/api/servers`, fall back to the list when a REST detail refresh is rejected, and answer local one-shot smoke probes.
- Support local favorites, recent servers, manual direct-server entries, search/filter/sort controls, and safe launch-argument previews.
- Provide a Morrowind-styled manager shell with a Mod Setup workspace for OpenMW-compatible `--data` and `--content` launch arguments from local profile exports.
- Provide an Admin Editor workspace with structured banlist and required-data-file edits, account/world indexes, and guarded XML source saves with adjacent backups.
- Use a frameless Tauri window with in-app drag, minimize, maximize, close controls, monitor-aware startup sizing, and adaptive internal scrolling for smaller displays.
- Keep account username handoff separate from character names and account passwords. The launch plan includes `--name` for the server account and can include a server join password, but it never includes an account password.
- Continue porting GNS-native detail queries, direct game-server ping timing, and full details payloads into this replacement.

## Build

- From CMake: `cmake --build <build-dir> --config RelWithDebInfo --target communitymp-hub`.
- From this folder: `npm run build:exe`.
