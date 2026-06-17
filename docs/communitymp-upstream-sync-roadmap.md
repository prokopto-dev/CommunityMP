# CommunityMP Upstream Sync Development Log and Roadmap

Last updated: 2026-06-16

This document is an internal engineering decision log for the CommunityMP/OpenMW
upstream-sync effort. It preserves technical context, review themes, risk calls,
and release sequencing for the multiplayer fork.

## Current Direction

CommunityMP is being maintained as a multiplayer-first OpenMW fork. The working
rule is to keep upstream OpenMW behavior wherever it improves correctness,
modding compatibility, rendering, physics, scripting, or editor stability, then
add multiplayer-specific guards where a singleplayer assumption would create
server/client desync.

Active upstream sources:

- OpenMW `openmw-1.0` milestone issues.
- OpenMW `openmw-0.52` milestone issues.
- The open work-items queue and boards.
- Issues and merge requests with clear client, editor, server scripting, or mod
  compatibility impact for TES3MP/CommunityMP.

## Recent Engineering Notes

### Multiplayer Equipment Regression

Problem:

- Local player equipment changes could be immediately overwritten by repeated
  `ID_PLAYER_EQUIPMENT` packets from the server.
- In practice this looked like items auto-unequipping after the player equipped
  them.

Decision:

- Treat equipment updates as player-scoped traffic.
- Apply compact equipment packets only to the slots explicitly listed in the
  packet, while full equipment snapshots still reconcile every slot.
- Send full authoritative equipment snapshots when the server corrects a
  rejected or stale client equipment packet.
- Preserve equipped items more carefully in the server-side player scripts.
- Tolerate harmless charge/count representation differences when deciding
  whether an equipped item is still the same item.

Rationale:

- Equipment is a high-frequency multiplayer state boundary. A strict
  singleplayer-style inventory rewrite is too destructive when the local client
  and server are converging on the same state.
- Server authority is still preserved, but the server should not cause needless
  churn when the item identity is unchanged.
- Compact packets are delta messages. Applying empty cached entries from slots
  not named by the packet can create false unequips, especially during login,
  reload, or script-driven equipment corrections.

Follow-up:

- Add a multiplayer smoke that equips armor, weapon, and clothing while the
  server sends periodic inventory/equipment sync.
- Record packet timing so regressions can be separated into transport ordering,
  server script state, and client application behavior.

### Multiplayer Cell Transitions: Server-Owned Followers

Problem:

- Door and interior transitions are hard simulation boundaries: a guard or NPC
  chasing a player's old exterior coordinates must not be accepted into the new
  interior just because a client still had local AI state.
- Blocking all actor door transitions prevents that failure mode, but it also
  leaves legitimate player followers without a server-owned way to cross the
  same accepted boundary.

Decision:

- Remember each player's last server-accepted cell.
- When the server accepts a player cell-change packet, scan the previous
  accepted source cell for live actors whose cached AI package is `FOLLOW` or
  `ESCORT` targeting that player.
- Move only those actors across the accepted boundary as server-generated
  follower cell changes, place them in a finite destination-side formation
  behind the player using the player's yaw, persist the generated actor
  cell-change through the existing Lua save path, and send the move to source
  and destination visitors plus the transitioning player.
- Keep ordinary combat/chase AI blocked across different simulation cells.

Rationale:

- Followers should cross doors because the server accepted the player's
  transition and owns the actor's cached AI package, not because a local client
  continued simulating singleplayer AI through a stale cell boundary.
- Destination-side placement avoids reusing exterior/interior coordinates across
  incompatible cell spaces, which is the class of bug that can produce stuck
  doors, wall-walking actors, or apparent random teleports.

Verification:

- The actor movement/AI guard pins accepted-cell tracking, follow/escort target
  filtering, deterministic yaw-based follower placement, Lua persistence staging,
  C++ actor cell-cache migration, and single delivery to the transitioning
  player.
- `tes3mp-server` and `components-tests` build in `RelWithDebInfo`.

### Multiplayer Cell Transitions: Atomic Server Locations

Problem:

- Server-driven teleports such as chargen completion and interior/exterior door
  exits can send a cell change immediately followed by a position update.
- If the cell packet is built before the destination transform is staged, the
  local client can enter the destination cell using an exterior-cell fallback
  origin, acknowledge that fallback, and make the server accept a jump to the
  wrong region.

Decision:

- Stage server position and rotation before sending server-owned cell-change
  packets from `SendLocation`, configured respawn, and saved-character cell
  load paths.
- When a local player receives a server cell change that already carries a
  finite position snapshot, use that packet position for exterior, exterior by
  name, and interior transitions before sending the acknowledgement.
- Keep the legacy exterior-origin and `fixPosition()` fallback only for packets
  without a valid server position.

Rationale:

- A server-owned cell change is a single simulation state update: destination
  cell plus destination transform. Splitting those across two client frames lets
  the client briefly simulate from an invalid coordinate basis and echo that
  invalid basis back to the authority.

Verification:

- The player movement guard checks that server-driven cell changes apply finite
  packet positions before acknowledgement and that `SendLocation` stages
  position/rotation before `SendCell`.
- Server location acknowledgements compare exterior descriptions by grid, so a
  pending `-2, -9` transition can be acknowledged as `Seyda Neen (-2, -9)`
  without being downgraded to an ordinary client-authored cell change.
- Client-authored exterior cell changes are corrected unless the reported
  position maps to the claimed exterior grid, using the OpenMW
  `floor(position / 8192)` cell-index geometry with a small boundary allowance.
- After a local cell change, the client ignores server position packets whose
  movement sequence is older than the cell-change snapshot. This prevents an
  interior-space transform from being applied in the exterior cell that just
  loaded, which otherwise maps small local coordinates into exterior grid
  `(-1, -1)` and causes the West Gash snap.
- Local door/cell changes no longer send a standalone `ID_PLAYER_POSITION`
  before the reliable `ID_PLAYER_CELL_CHANGE`. The cell-change packet carries
  the destination transform with the destination cell, so the server does not
  judge a legal door teleport against the old coordinate space.
- If the server receives a cell-less movement snapshot whose horizontal
  discontinuity is large enough to imply an interior/exterior coordinate-space
  transition, it rejects the snapshot without sending an old-cell correction
  and waits for the reliable cell-change packet.
- Character persistence and death/revive guards check the same ordering for
  instanced chargen spawns, saved character loads, and configured respawns.

### Chargen Release State Replay

Problem:

- New characters can finish chargen and be moved out of the Census Office while
  the client is still processing the server-owned spawn.
- Sending tutorial-release item, journal, and topic packets before that spawn is
  acknowledged can leave the saved character state correct but the live client
  missing the release papers, directions, gold, dialogue topics, or
  `a1_1_findspymaster` journal stage.

Decision:

- Preserve the new-character flag across provisional chargen quicksaves, then
  apply the release state to server storage during `EndCharGen` before the
  final account/character save.
- Queue the outbound release packets when the server sends the chargen spawn
  location, then replay them after the first valid destination `OnCellLoad` or
  the matching `chargenSpawn` `PlayerCellChange` acknowledgement, whichever
  arrives first.
- Keep the old immediate replay for configurations where no server spawn is
  sent.

Rationale:

- The chargen release is part of accepted server character state, but the live
  packet replay must be ordered after the authoritative cell/position
  transition so the client applies it to the loaded gameplay character instead
  of the disappearing chargen state.

Verification:

- `check-tes3mp-character-persistence-sync.ps1` guards release state mutation,
  queued packet replay, post-cell-load/post-ack flush, and no-spawn fallback
  behavior.
- Lua compatibility coverage verifies both the immediate no-spawn path and the
  delayed destination-cell-load and `chargenSpawn` acknowledgement paths.

### Multiplayer Simulation Authority: Active Cells and Movement Reasons

Problem:

- A singleplayer client used to be the only simulation owner, so local movement,
  door transitions, recall/intervention teleports, AI, combat, and scripts all
  happened in the same process.
- In multiplayer, accepting raw client position or cell reports lets a bad
  ordering bug, stale AI state, or malicious packet rewrite the shared world.
- Simulating every cell at full rate is not required for the current server
  authority goal and would waste CPU on places no player can observe yet.

Decision:

- Treat the server as the owner of accepted player transforms, movement
  sequence history, movement mode, cell membership, actor AI state, combat
  outcomes, and script-driven state changes.
- Clients may send movement snapshots during this transition period, but the
  server treats them as bounded intent and drift observations, not final
  authority. Accepted player transforms are simulated from the last accepted
  transform plus sanitized movement direction, while large client/server drift
  triggers a reliable correction back to the sender.
- Discontinuous movement must have a server-visible reason: server command,
  door/portal activation, recall/intervention or scripted magic, quest move,
  respawn, or admin teleport. Ordinary cell-change packets are not enough by
  themselves to prove a teleport is legal.
- Simulate active cells first: cells containing players plus the currently
  relevant loaded exterior neighborhood, interiors, combat participants,
  followers, summons, and quest/script actors attached to those players.
- Persist inactive cells and resume them when interest returns. Add low-rate
  offscreen simulation later only for systems that explicitly need it.
- Keep the shared world as the default. Use per-player or party instances only
  for quest-critical spaces, template respawns, progression blockers, or content
  that must not be consumed globally.

Rationale:

- This matches the immediate multiplayer requirement: everyone sees the same
  authoritative result in the places players actually occupy, while the server
  avoids spending full simulation budget on empty wilderness and interiors.
- Legal movement reasons turn door exits, magic teleports, and quest moves into
  auditable server state instead of trusting a client to report impossible
  coordinates after the fact.
- Accepted object activations now create a short-lived client transition reason
  that the next cell-change packet must consume from the same source cell or
  exterior grid alias. The server logs unreasoned discontinuous client cell
  changes so remaining door, magic, quest, respawn, and admin transition paths
  can be tightened without confusing them with ordinary movement.
- Door destination packets are now server-visible state. The dedicated server
  processes `ID_DOOR_DESTINATION`, exposes the teleport flag, destination cell,
  position, and rotation to Lua, saves them in cell object data, and replays
  them during initial cell load.
- When a player activates a cached teleport door, the pending client transition
  is constrained to that door's actual OpenMW destination cell. A later
  `PlayerCellChange` to another cell is rejected before persistence and the
  server sends the last accepted cell and transform back to the client.
- Player cell-change packets now carry a compact transition reason from the
  OpenMW source path. Doors, Recall, Divine/Almsivi Intervention, respawns,
  guided travel, script teleports, jail teleports, and server-authored
  corrections are distinguishable from ordinary exterior streaming. The server
  rejects untyped interior/exterior swaps and non-adjacent exterior jumps before
  Lua persistence, while Lua records the same reason in `playerPacket.location`.
- Server-authored Lua movement can now set the outgoing reason explicitly with
  `SetCellChangeReason(pid, reason)` before `SendCell`. `BasePlayer:SendLocation`,
  configured respawns, saved-cell loads, and admin/player teleports carry a
  typed pending reason through acknowledgement, so hooks and persistence can
  distinguish server moves, respawns, and script/quest moves.
- Client-side transition sources are explicitly typed before their cell-change
  packet is sent. Doors, Recall, Divine/Almsivi Intervention, guided travel,
  jail, local respawn markers, console/script teleports, and MWLua player
  teleports queue a non-normal cell-change reason; the local player resets the
  reason only after sending the cell-change packet.
- The server actor tick now uses an explicit active-cell predicate. Cells with
  loaded players are simulated, and empty cells are skipped unless a server
  script opts them in through `SetCellSimulationInterest(cellDescription, true)`.
  That keeps authoritative AI/movement scoped to observed or intentionally
  retained cells while preserving a path for quest/script actors that must
  continue without a player standing in the cell.
- Cached `WANDER` AI packages now produce server-side movement in those active
  cells. The server keeps a per-actor wander origin and deterministic decision
  sequence, chooses bounded destinations inside the AI distance, normalizes the
  movement vector, advances the actor transform, and broadcasts the resulting
  actor position packet. This is the first server-owned AI movement primitive;
  full navmesh/pathfinding, schedules, and offline simulation remain follow-up
  work.
- Actor position and `ActorAI` packets are treated as loaded-cell observations,
  not client-owned truth. The sending player must still have the cell loaded,
  but `ServerSimulation` validates known/live actors, rejects invalid AI targets
  or stale movement, normalizes accepted state against the server cache, stores
  it as canonical cell actor state, and rebroadcasts that canonical state to
  loaded visitors.
- Once the server has a cached AI package and accepted position for an actor,
  that actor's movement is server-owned. Later client `ActorPosition`
  observations for `WANDER`, `TRAVEL`, `ESCORT`, `ACTIVATE`, `COMBAT`, and
  `FOLLOW` actors are answered with the server's canonical transform instead of
  being allowed to steer the shared NPC.

Verification:

- Movement and combat guards pin accepted transform sequences, server-side
  plausibility correction, cast/attack target validation, and reliable
  cell-change snapshots.
- The movement-sync guard now pins door-destination packet processing, Lua
  persistence/replay, destination-constrained door activation, and typed
  server-authored and client-origin cell-change reasons.
- The actor movement/AI guard now pins active-cell simulation filtering, the Lua
  simulation-interest API, deterministic server-side `WANDER` movement, plus
  loaded-cell actor position and `ActorAI` acceptance through the server
  simulation layer.

### Multiplayer Quest State: Character Hydration

Problem:

- Saved inventories, cooldowns, active spells, faction state, client script
  globals, map exploration, kill counts, journals, topics, read-book flags, and
  spellbooks belong to the selected character or shared-world state, but the old
  load path only appended server entries to the client.
- Large saves could exceed the `ID_PLAYER_INVENTORY`, `ID_PLAYER_JOURNAL`,
  `ID_PLAYER_COOLDOWNS`, `ID_PLAYER_FACTION`, `ID_PLAYER_TOPIC`, and
  `ID_PLAYER_BOOK` decoder cap of 3000 changes per packet, and large spellbook
  snapshots could exceed the same cap on `ID_PLAYER_SPELLBOOK`; large
  client-global, map, and kill snapshots could exceed the same worldstate caps,
  making a high-volume character or shared journal snapshot invalid on load.

Decision:

- Treat journal, topic, and read-book loads as explicit server hydration
  packets. The first load batch clears the matching local client state, journal
  load batches suppress gameplay journal popups, and a final empty non-load
  packet closes each hydration window.
- Split saved journal, topic, and read-book snapshots into batches of at most
  3000 changes, matching the packet decoder cap. This keeps large
  per-character and shared journal/topic state on the existing authoritative
  save path without requiring Redis as a hard dependency.
- Save accepted journal and topic deltas through materialized lookup tables
  instead of rescanning the full saved list for every incoming change. Journal
  entry dedupe remains case-insensitive by quest and index, journal index and
  finished states remain last-write-wins per quest, and journal/topic metadata
  revisions now advance when accepted deltas change the materialized state.
  Accepted saves return only the deltas that actually changed server state, and
  journal/shared-journal fanout rebuilds outbound packets from those accepted
  deltas instead of echoing raw client packets.
- Record accepted journal and topic changes in bounded revision logs and expose
  revision catch-up helpers. The current JSON save path remains canonical, but
  the same accepted-delta/revision contract gives a future Redis Streams or
  similar online backend a stable insertion point without becoming a required
  runtime dependency.
- Reconcile missed online ally journal state through the same materialized
  index model: duplicate entries are skipped, journal stages only advance,
  finished states progress from missing/false to true, and accepted ally merge
  changes advance the receiving character's journal revision before the
  authoritative reload is sent.
- Split saved spellbook snapshots with an initial `SET` batch followed by `ADD`
  batches, preserving the existing client-side clear/apply behavior while
  staying under the packet cap.
- Split saved inventory snapshots with the same initial `SET` then capped `ADD`
  batch pattern, preserving metadata and the existing authoritative client
  clear/apply behavior.
- Split saved cooldown, active spell, and faction snapshots into capped
  batches, preserving the existing cooldown append behavior, active-spell
  `SET`/`ADD` hydration, and the rank/expulsion/reputation faction actions.
- Split saved client-script globals, map tiles, and player/world kill counts
  into capped batches only because their client apply paths merge/apply entries
  independently; destination overrides are left unbatched until they have an
  explicit merge or load-marker contract.

Verification:

- `check-tes3mp-journal-topic-sync.ps1` pins explicit topic load markers,
  one-shot client clearing, journal/topic batching, and packet-cap constants.
- Component packet coverage verifies topic and read-book load-marker
  round-tripping and the oversized-count packet tests use the correct
  journal/topic/book payload layout.
- Server Lua compatibility coverage verifies 3001 journal/topic/book entries
  load as `3000 + 1 + completion marker` batches.
- Server Lua compatibility coverage verifies large journal/topic save deltas
  update through materialized indexes, skip duplicate entries/topics, preserve
  last-write-wins journal state, return only accepted deltas, record bounded
  revision logs, expose revision catch-up helpers, and advance journal/topic
  revisions.
- Server Lua compatibility coverage verifies online ally journal reconciliation
  uses materialized indexes, advances only accepted journal state, and reloads
  changed allies from the authoritative server state.
- Server Lua compatibility coverage verifies a 3001-spell saved spellbook loads
  as a `3000` spell `SET` batch followed by a `1` spell `ADD` batch.
- Server Lua compatibility coverage verifies a 3001-item saved inventory loads
  as a `3000` item `SET` batch followed by a `1` item `ADD` batch.
- Server Lua compatibility coverage verifies 3001 saved cooldowns and each
  3001-entry faction action load as `3000 + 1` batches.
- Server Lua compatibility coverage verifies 3001 saved active spell instances
  load as a `3000` spell `SET` batch followed by a `1` spell `ADD` batch.
- Server Lua compatibility coverage verifies 3001 client globals, map tiles,
  personal kill counts, and shared-world kill counts load as `3000 + 1`
  batches.

### Input: High-DPI Mouse-Look Precision

Problem:

- OpenMW issue #8986 tracks jerky in-game mouse input where slow fullscreen
  mouse motion can fail to move the camera, while menu cursor movement remains
  usable.
- The SDL input wrapper computed drawable-to-window scale using integer
  division and stored the result in integer fields.
- The in-game camera consumed integer relative deltas, so fractional
  high-DPI/fractional-scaling motion could be truncated before it reached
  mouselook.

Decision:

- Compute SDL drawable scale as finite positive floats, falling back to `1.0`
  when SDL reports invalid dimensions.
- Add precise relative mouse deltas to the packaged `MouseMotionEvent`.
- Keep the legacy SDL-style integer relative fields populated by rounding so
  existing binding consumers still receive ordinary integer motion.
- Use the precise relative deltas for camera mouselook and controller-tooltip
  unhide thresholds.

Rationale:

- The underlying issue is input precision loss before gameplay code sees the
  motion, so preserving precision at the event boundary fixes camera look
  without broad camera or settings changes.
- Rounded integer compatibility keeps older input consumers stable while
  allowing the high-frequency camera path to respond to subtle motion.

Verification:

- The upstream work-item guard checks float drawable scaling, precise relative
  event fields, rounded compatibility fields, first-event suppression, and
  mouselook using the precise deltas.
- Runtime validation should include fullscreen and borderless modes on a
  fractional-scale desktop, with slow horizontal and vertical mouselook passes.

Multiplayer note:

- This is a local client input fix. It does not alter packet format, server
  movement authority, or saved input settings, but it makes remote play testing
  less error-prone by restoring fine-grained local camera control.

### Physics: Transformed Static Collision Meshes

Problem:

- Several OpenMW upstream physics issues point at transformed triangle collision
  meshes behaving inconsistently in Bullet.
- Non-orthogonal or otherwise transformed static meshes can jitter or collide at
  the wrong shape/position because the transform is carried separately instead
  of baked into the mesh vertices.

Decision:

- Bake non-identity transforms into static triangle mesh vertices in the NIF
  Bullet loader.
- Leave animated collision transform-driven so runtime animation can keep moving
  the collision shape.

Rationale:

- Static collision is safest when the final Bullet triangle mesh already
  contains the transform. This reduces runtime ambiguity and matches the common
  expectation for fixed world geometry.
- Animated collision must remain dynamic; baking it would freeze data that is
  supposed to move.

Verification:

- Bullet NIF loader tests cover non-orthogonal static transforms, ordinary
  orthogonal transforms, and mixed static-plus-animated children.
- Focused `components-tests` Bullet NIF suite passed.

Multiplayer note:

- This is client-side physics correctness. It does not change packet format or
  server authority, but it improves agreement between clients when mods rely on
  transformed collision assets.

### Editor: Legacy Record ID Length Verification

Problem:

- OpenMW issue #6778 asks for OpenMW-CS verification feedback when a record ID
  is too long.
- CommunityMP servers often combine older Morrowind-era plugins with newer
  OpenMW-friendly mods, so record IDs that work locally can still cause legacy
  tooling or old fixed-size ESM3 fields to behave poorly.

Decision:

- Add a dedicated OpenMW-CS verifier stage for top-level record collections.
- Warn when a string-backed record ID is longer than 32 bytes.
- Ignore deleted records and non-string IDs such as skill and magic-effect
  indexes.
- Skip editor-internal subrecord/composite IDs so topic-info and instance
  bookkeeping do not create false positives.

Rationale:

- A warning fits the compatibility risk better than a hard verifier error:
  OpenMW may support longer IDs in some paths, but Morrowind-compatible data and
  legacy fields remain risky.
- Keeping this as a generic verifier stage makes it easy to audit and avoids
  duplicating the same length rule across individual record-type checkers.

Verification:

- `openmw-cs-tests` includes focused coverage for the 32-byte boundary,
  warning severity, deleted records, and non-string IDs.
- The upstream work-item guard tracks the verifier stage, the 32-byte limit, the
  warning text, and the test fixture.

Multiplayer note:

- This is editor-side compatibility work. It does not change network protocol
  or runtime authority, but it helps server admins catch problematic plugin
  records before they reach a shared world.

### Physics: Stationary Seated NPC Placement

Problem:

- OpenMW issue #3803 tracks seated NPC mods being pushed upward onto nearby
  furniture after spawn.
- The final ground snap in actor movement can still run for an actor that did
  not request movement and has no inertial force, so mod-authored static poses
  can be treated as something to correct.

Decision:

- Preserve placement for non-player actors that are already grounded, not on a
  slope, not flying, not water-colliding, and have zero movement and zero
  inertia.
- Keep the normal ground test for players, moving actors, falling actors,
  flying actors, and water-collision cases.

Rationale:

- The old patch attached to #3803 treated every zero-velocity actor as grounded,
  which is too broad for a multiplayer fork.
- The narrower rule targets seated/static NPC mods and remote stationary actors
  without weakening player physics or server-authoritative movement updates.

Verification:

- The upstream work-item guard checks that this stationary-actor path remains
  scoped to non-player grounded actors with zero movement and zero inertia.
- Follow-up runtime testing should use Animated Morrowind sitting NPCs and at
  least one remote stationary actor replicated from the server.

Multiplayer note:

- This avoids client-side vertical correction fighting server-authored actor
  placement for idle NPCs. Packet format and server scripts are unchanged.

### Physics: One-Sided Open Surface Collision

Problem:

- OpenMW issue #806 tracks vanilla behavior for non-closed/open models: actors
  can pass through the reverse side of some authored surfaces, such as interior
  kit walls, boulders, and ceiling pieces.
- Bullet convex sweeps report a usable collision normal, but for triangle mesh
  contacts that normal can behave like a two-sided sweep result rather than the
  authored triangle front face.

Decision:

- For actor sweeps against world triangle meshes, confirm the hit with a short
  target-only ray probe using Bullet's triangle backface filter.
- Reject the sweep hit when the probe cannot hit the authored front face.
- Keep existing behavior for actors, projectiles, heightmap terrain, convex
  shapes, and non-world collision groups.

Rationale:

- This keeps the compatibility change scoped to the open-surface case from
  #806, without turning every collision object into one-sided geometry.
- The target-only probe works with compound NIF collision objects, which is
  important because most world assets arrive as compound Bullet shapes in
  OpenMW.

Verification:

- The upstream work-item guard checks that the actor sweep callback uses
  Bullet's `kF_FilterBackfaces` probe and applies it only to world triangle
  hits with triangle shape info.
- Runtime validation should include an interior kit wall from the void side, a
  closed static from the normal side, and a multiplayer client/server movement
  pass to verify that server correction does not fight the local pass-through.

Multiplayer note:

- The change is local physics compatibility. It does not alter packets, but it
  can change the accepted local actor position before the normal movement sync
  layer serializes or receives correction.

### Rendering: Magic VFX Particle Scaling

Problem:

- OpenMW issue #3559 tracks spell effect particle systems scaling differently
  from the original engine.
- Large area effects, such as Light area VFX, could make particle billboards
  much larger than intended because the effect transform scaled both mesh
  geometry and particle size.

Decision:

- Keep particle positions and mesh geometry under the normal effect transform.
- For magic VFX nodes, switch particle billboard sizing to world coordinates so
  the NIF-authored particle size remains stable when the effect node is scaled.
- Apply the rule to both attached actor/object effects and free world effects.
- Leave non-magic free effects and ordinary placed object particles on the
  default local-size behavior.

Rationale:

- The original engine appears to scale the effect's geometry/placement without
  scaling particle billboard size.
- Scoping the rule to magic VFX avoids shrinking intentionally scaled object
  particles, such as placed lights or modded animated statics.

Verification:

- The upstream work-item guard checks the shared particle-size visitor and both
  magic VFX call sites.
- Follow-up runtime validation should compare small and large area spells that
  use particle-heavy area models.

Multiplayer note:

- This is a deterministic client-side rendering fix. It does not affect world
  state, packet data, hit detection, or server authority.

### Rendering: Actor-Attached Particle Bounds

Problem:

- OpenMW issue #9036 tracks actors with particle systems accumulating oversized
  bounding spheres.
- World-space particle effects, such as smoke from a carried torch, can leave
  live particle positions behind as the actor moves. If those positions become
  the drawable bound, the actor can appear to occupy every place the particles
  have existed.

Decision:

- Keep NIF particle drawables bounded by their authored initial bound when that
  bound is valid.
- Fall back to the normal OSG particle dynamic bound only for particle systems
  that do not provide a valid authored bound.

Rationale:

- NIF particle data already carries a bound intended to cover the authored
  emission space. Using it prevents old world-space particles from stretching
  the parent actor's scene bounds.
- The fallback preserves behavior for programmatic particle systems that are
  not sourced from NIF controller data.

Verification:

- A focused `components-tests` case moves a live NIF particle far outside the
  authored initial bound and verifies `computeBoundingBox()` still returns the
  authored bound.
- The upstream work-item guard checks the override, the fallback, and the
  regression test.

Multiplayer note:

- This is a client-side culling/performance fix. It does not change packets or
  gameplay authority, but it reduces needless traversal cost for replicated
  actors carrying particle-heavy equipment or spell effects.

### Rendering/Performance: Authored Static NIF Bounds

Problem:

- OpenMW issue #8982 tracks OpenMW ignoring bounding spheres authored into NIF
  geometry data and relying on OSG recomputation instead.
- OSG's fast recomputed spheres can be much larger than exporter-authored
  spheres, which can reduce culling, lighting, raycast broad-phase, and physics
  broad-phase efficiency on complex modded meshes.

Decision:

- For static, unskinned `NiGeometry`, use the NIF-authored
  `NiGeometryData::mBoundingSphere` as the OSG node bound.
- Transform the authored local sphere through the NIF node transform before
  attaching it to the generated OSG node.
- Preserve custom compute-bound callbacks through static transform flattening
  and keep callback-bearing nodes out of redundant-node/group merge removal.
- Reject missing, non-finite, animated, skinned, controller-driven, or
  non-enclosing authored bounds and keep OSG's normal computed bound in those
  cases.

Rationale:

- Static mesh bounds are the case where exporter-authored spheres are most
  useful and least risky.
- The vertex-enclosure check protects older or malformed assets from being
  culled too aggressively while still allowing optimized MOP/OAAB/TD-style
  assets to provide tighter broad-phase bounds.

Verification:

- `components-tests` verifies a static `NiTriShape` uses the transformed
  authored sphere.
- `components-tests` verifies the authored sphere survives the same
  static-transform and redundant-node optimizer passes used by scene templates.
- `components-tests` verifies an authored sphere that does not enclose the
  mesh vertices is rejected.
- The upstream work-item guard checks the OSG callback, finite-bound checks,
  vertex-enclosure check, animation/skinning exclusions, transform application,
  optimizer callback preservation, and regression tests.

Multiplayer note:

- This is deterministic client-side scene optimization. It does not alter
  packets, server authority, collision shapes, or gameplay state, but it can
  reduce per-client render and broad-phase work for heavily modded multiplayer
  cells.

### Physics: Non-Finite Object Rotations

Problem:

- OpenMW issue #8390 tracks content with NaN object rotations upsetting Bullet
  collision in modded cells.
- A non-finite Euler angle can become a non-finite OSG or Bullet quaternion
  during scene insertion, rotation updates, or collision object creation.

Decision:

- Treat non-finite rotation angles as zero-angle components in the shared
  OSG/Bullet conversion helpers.
- Use the same sanitizer when applying world rotation changes before writing the
  new rotation back to object ref data.

Rationale:

- This keeps malformed content from poisoning render or physics transforms while
  preserving every finite rotation component.
- Sanitizing at the conversion boundary also protects multiplayer object and
  actor placement paths that reuse the same world rotation helpers.

Verification:

- A focused `components-tests` case feeds NaN and infinite Euler angles into the
  OSG and Bullet conversion helpers and verifies finite identity quaternions.
- The upstream work-item guard checks the sanitizer, conversion helpers, world
  rotation write path, scene insertion path, and regression test.

Multiplayer note:

- CommunityMP already rejects non-finite network position packets in several
  actor/object paths. This adds equivalent protection for malformed local
  content before it can destabilize client-side physics.

### Physics/Rendering: Non-Finite Object Scales

Problem:

- OpenMW issue #8969 includes OSG cull warnings with NaN matrices while changing
  cells in a modded setup.
- Object reference scale is already clamped for finite `XSCL` values, but NaN
  can pass through `std::clamp` and poison render, physics, map, and navigator
  transforms.

Decision:

- Treat non-finite `XSCL` values as the default scale `1.0` when loading cell
  references.
- Do not write non-finite scales back into saved cell references.
- Apply the same non-finite fallback in the runtime cell-ref setter used by
  MWScript `SetScale`/`ModScale` and Lua object scaling.

Rationale:

- A default scale is safer than propagating NaN into OSG/Bullet transforms, and
  it preserves the existing clamp for every finite authored scale.
- Runtime sanitization protects multiplayer clients from local scripts or admin
  tools accidentally producing non-finite visual or collision transforms.

Verification:

- `components-tests` loads a raw NaN `XSCL` cell reference and verifies the
  result is scale `1.0`.
- `components-tests` verifies non-finite cell-ref scales are not serialized
  back out as `XSCL`.
- The upstream work-item guard checks the load/save sanitizer, runtime setter,
  and regression tests.

### Rendering: Scoped NIF Transparency Sorting

Problem:

- OpenMW issue #3366 tracks flickering transparent cave crystals caused by
  inconsistent transparency rendering order.
- Related reports point at NiSortAdjustNode subsort behavior, where accumulator
  choices are expected to affect a local subtree rather than unrelated sibling
  nodes.
- The importer carried the active sort-adjust state as global traversal state
  and did not restore the previous state after leaving a node subtree.

Decision:

- Save the current NiSortAdjustNode context when entering each imported NIF
  node.
- Let children inherit the active sorter while the traversal remains inside the
  subtree.
- Restore the previous active sorter and previous non-inherit fallback before
  returning to the parent node.

Rationale:

- This keeps sort-adjust behavior lexical to the authored NIF hierarchy and
  prevents stale accumulator state from leaking to later siblings.
- Stable local sorting is safer for modded multiplayer clients because every
  client importing the same asset should derive the same transparent draw-bin
  assignment.

Verification:

- The upstream work-item guard checks that sort context is saved, used for
  NiSortAdjustNode processing, and restored after subtree traversal.
- Follow-up runtime validation should include Punabi-style purple cave crystals
  and a modded asset with nested or sibling sort-adjust nodes.

Multiplayer note:

- This is a deterministic client-side rendering fix. It changes scene-graph
  state assignment only during model import and does not alter packets, server
  scripts, physics, or gameplay authority.

### Interaction: Terrain-Ignoring Focus Raycasts

Problem:

- OpenMW issue #8822 tracks a vanilla compatibility gap where basic interaction
  raycasts should be able to see barely exposed or buried activatable objects
  through terrain.
- Mods can rely on this behavior for buried treasure or objects that are only
  visible by a few pixels above the landscape.

Decision:

- Add an explicit `ignoreTerrain` option to camera-to-viewport render raycasts.
- Opt the built-in focus-object path into terrain skipping.
- Leave generic render rays, Lua-facing raycasts, object placement, and
  drop-to-ground logic terrain-aware by default.

Rationale:

- The compatibility rule is interaction-specific; broadening it to every render
  ray would change scripting, placement, and editor-style workflows in ways that
  mods and server logic may already depend on.
- Keeping the behavior opt-in makes the multiplayer boundary easy to audit:
  local activation targeting changes, but script-visible raycast semantics and
  server-side state synchronization remain stable.

Verification:

- The upstream work-item guard checks that focus-object lookup opts into terrain
  skipping while generic render rays explicitly keep terrain enabled.
- Follow-up runtime validation should include the buried Odai Plateau ebony
  shortsword case, an ordinary exterior activation target, object placement on
  terrain, and a Lua `nearby.castRenderingRay` probe that still reports terrain
  when expected.

Multiplayer note:

- This is a client targeting compatibility fix. It can change which object the
  local player is allowed to activate, but it does not alter packet format,
  equipment sync, persistence records, or server script APIs.

### Physics: Targeted Collision Toggle

Problem:

- OpenMW issue #7067 tracks the console `tcl` command only affecting the player.
- That makes it harder to unstick followers or other actors without moving them
  through normal physics first.

Decision:

- Add an explicit-reference `tcl`/`togglecollision` opcode.
- In console context, let bare `tcl` use the selected actor when the selected
  reference is an actor.
- Keep bare `tcl` in ordinary local/global scripts on the existing player-only
  behavior so old scripts do not suddenly toggle their own owner.

Rationale:

- The fix gives admins and testers a useful actor-recovery tool without changing
  persisted actor state or packet layout.
- Non-actor console selections fall back to the player path, preserving the
  common habit of clicking a wall or landscape and running `tcl`.

Verification:

- The upstream work-item guard checks explicit opcode registration, console-only
  implicit targeting, explicit target validation, player fallback behavior, and
  actor collision toggle/position-adjust behavior.
- Follow-up runtime validation should select a follower/NPC in the console, run
  `tcl`, move or script-place the actor through obstructing geometry, run `tcl`
  again, and confirm the actor is snapped back onto valid collision.

Multiplayer note:

- This is intentionally local console/script behavior. It does not introduce a
  new networked actor collision flag; multiplayer servers should continue to use
  existing teleport/position authority when moving actors between clients.

### Physics: Nested No-Collision Extra Data

Problem:

- Some NIF files use nested `NC`/`NCO` string extra data to disable collision for
  a node subtree.
- The previous handling focused on root-level extra data and could generate
  collision for nested subtrees that should have been skipped.

Decision:

- Apply string extra data while traversing nodes.
- Preserve root-level visual collision behavior.
- For nested `NC`/`NCO`, skip the affected subtree while keeping sibling
  collision intact.

Rationale:

- This matches how artists and modders author collision exclusions in real
  assets.
- The subtree rule is narrow and avoids disabling unrelated sibling geometry.

Verification:

- Tests cover subtree skipping and sibling preservation.

### Client GUI and Rendering

Problem:

- The minimap should be zoomed out relative to the local map for vanilla-style
  readability.
- Non-shader water ripples can flicker or darken when camera movement interacts
  with simple lighting.

Decision:

- Keep the HUD minimap zoom defaulted to `0.5`.
- Size local-map widgets from the current zoomed widget size rather than assuming
  a fixed 1x tile size.
- Keep simple water ripples emissive with low alpha so lighting does not tint or
  flicker them.

Rationale:

- These are local client presentation fixes and do not affect multiplayer packet
  compatibility.
- They reduce visual mismatch between clients without changing authoritative
  world state.

### Editor: Distortion Render Bin Fallback

Problem:

- OpenMW issue #8784 tracks OpenMW-CS logging
  `RenderBin "Distortion" implementation not found` when previewing assets that
  request the runtime distortion pass.
- The game runtime registers a full postprocessing-backed `Distortion` bin, but
  the editor scene widget does not own the runtime postprocessor or its
  framebuffer targets.

Decision:

- Register a lightweight `Distortion` render-bin prototype when an OpenMW-CS
  render widget is constructed.
- Keep the fallback sorted back-to-front.
- Override the distortion node depth state back to normal editor depth testing
  instead of enabling the runtime distortion shader path without its required
  textures.

Rationale:

- This removes noisy render-bin fallback warnings and gives OpenMW-CS a stable
  preview path for assets that use distortion metadata.
- Avoiding the runtime postprocessor keeps editor startup and preview rendering
  isolated from game-only framebuffer setup.

Verification:

- The upstream work-item guard checks that the editor registers the `Distortion`
  bin prototype and applies normal depth testing to that fallback bin.
- Follow-up visual validation should open a `t_glb_terrwater_*` asset in
  OpenMW-CS and confirm the warning no longer appears.

### Editor: Unlit Selection Marker

Problem:

- OpenMW issue #8634 tracks OpenMW-CS selection markers being affected by the
  scene lighting preview mode.
- The transform marker is an editor affordance and should stay readable in Day,
  Night, and Bright modes instead of inheriting asset lighting.

Decision:

- Rebuild the selection marker with the existing unlit debug shader prefix.
- Protect the marker root from fixed-function lighting as well, keeping the
  existing depth-test override and render-bin ordering.

Rationale:

- The marker is not part of authored world content, so lighting preview modes
  should not change its apparent color or visibility.
- Using the existing debug shader path keeps the fix local to editor gizmo
  rendering instead of changing global scene-lighting behavior.

Verification:

- The upstream work-item guard checks that the marker uses the debug shader
  prefix and a protected lighting-off state.
- Follow-up visual validation should select an object in OpenMW-CS, switch
  between Day, Night, and Bright lighting modes, and confirm the marker remains
  consistently visible.

### Editor: Direction-Colored Cell Arrows

Problem:

- OpenMW issue #8626 tracks exterior cell arrows being useful but visually
  ambiguous without hovering to read the tooltip.

Decision:

- Keep the existing generated arrow mesh and picking behavior.
- Assign distinct hardcoded colors for north, west, south, and east.
- Use a darker shade for the back/side vertices so the arrow still reads as a
  simple 3D shape.

Rationale:

- Direction colors make adjacent-cell loading/navigation faster in repeated
  editor workflows.
- The change is visual-only and does not affect scene selection masks, cell
  loading, save data, or runtime multiplayer behavior.

Verification:

- The upstream work-item guard checks the direction color helper, darkened
  secondary shade, and per-vertex color assignment.
- Follow-up visual validation should open an exterior cell with all neighboring
  cell arrows shown and confirm north, west, south, and east are distinguishable
  without hovering.

### Editor: Selection Marker Pick Tolerance

Problem:

- OpenMW issue #8635 tracks transform marker handles being too precise to click
  because picking uses the visible marker geometry exactly.

Decision:

- Add a small screen-space marker pick pass before the normal exact scene pick.
- Filter that tolerance pass to `ObjectMarkerTag` hits only.
- Keep ordinary object, terrain, pathgrid, and cell-arrow picking on their
  existing exact line-intersection path.

Rationale:

- A pixel-radius pick pass matches the editor affordance used by tools like
  Blender and Godot without modifying the selection-marker asset.
- Filtering to marker tags avoids widening normal scene-object selection, which
  would be surprising in dense modded scenes.

Verification:

- The upstream work-item guard checks the polytope intersector, six-pixel
  radius, marker-tag filter, behind-rotate-marker rejection, and early marker
  pick before the existing exact line pick.
- Follow-up visual validation should select an object, attempt drag handles
  slightly off the visible axis/plane/rotation geometry, and confirm nearby
  ordinary objects are not selected by the widened marker pass.

### Editor: Pathgrid Edge Cleanup

Problem:

- OpenMW issue #8344 tracks rare malformed pathgrids after deleting all nodes
  and adding new nodes again.
- Stale edge rows can survive if their endpoints are already invalid and no
  longer match any selected node exactly.

Decision:

- While deleting pathgrid nodes, continue removing edges attached to selected
  nodes.
- Also remove any edge whose adjusted endpoints would point outside the
  remaining node range.

Rationale:

- A pathgrid edge is only valid if both endpoints refer to existing points.
- Treating out-of-range adjusted endpoints as delete-worthy prevents preexisting
  malformed edge rows from being preserved into later saves.

Verification:

- The upstream work-item guard checks remaining-node counting, stale-edge
  removal, adjusted endpoint validation, and safe casting only after validation.
- Follow-up manual validation should create a pathgrid, add edges, delete all
  nodes, add a new node, save, and verify no edge row references a non-existent
  point.

### Editor: Persistent Modify Command Indexes

Problem:

- OpenMW issue #8022 tracks an OpenMW-CS crash while committing a dialogue enum
  editor through `ModifyCommand::redo`.
- The reported stack dereferenced a stale `QModelIndex` while resolving the
  edited cell's parent.

Decision:

- Store `ModifyCommand` target indexes as `QPersistentModelIndex` values.
- Map proxy indexes to the source model before making them persistent.
- Validate the target and record-state indexes before redo/undo touches model
  data or nested header metadata.

Rationale:

- Undo commands can outlive table layout changes, nested row refreshes, or editor
  signal ordering details.
- A no-op invalid command is preferable to crashing the editor during an enum
  commit, especially in dialogue tables where combo boxes can emit immediately
  from popup selection events.

Verification:

- The upstream work-item guard checks persistent index storage, proxy mapping,
  nested header fallback, and redo/undo validity checks.
- Follow-up manual validation should edit dialogue enum fields from a nested
  dialogue row, then undo/redo after switching rows.

### Editor: Dialogue Result Script Line Endings

Problem:

- Dialogue result scripts could be saved with inconsistent line endings.
- That creates noisy diffs and compatibility issues for tools expecting the ESM3
  Windows-style CRLF convention.

Decision:

- Normalize dialogue result script line endings to CRLF when saving `BNAM`.
- Preserve the logical script text while normalizing lone LF and lone CR.

Rationale:

- The editor should write stable ESM output and avoid making project diffs noisy
  for script-only edits.

Verification:

- Parameterized ESM3 save/load test confirms all supported format versions save
  dialogue result scripts with CRLF.

### Editor: Deleted Instance Subviews and Verification Hints

Problem:

- Deleting an instance from an opened instance subview could leave stale entries
  in the Instances or Verification UI.
- Attempting to show one of those stale entries could throw an exception because
  the reference ID no longer existed in the CellRef collection.

Decision:

- Do not open a reference subview if the reference is missing or already deleted.
- Do not resolve a scene `r:ref#...` hint if the reference is missing or marked
  deleted.

Rationale:

- Verification reports are snapshot-style until refreshed. They may temporarily
  contain stale references after a user edits data.
- The UI should ignore stale navigation targets instead of crashing or requiring
  a plugin reopen.

Follow-up:

- Consider a later report-table enhancement that visually marks stale report rows
  and offers a one-click refresh after destructive edits.

### Editor: Viewed Instance Scene Marker

Problem:

- OpenMW issue #2599 tracks a common OpenMW-CS navigation problem: using View on
  an instance row opens the correct scene, but the target instance can be hard
  to find in cluttered interiors, caves, or dense exterior cells.
- Scene hints already load the target cell, but they did not select or mark the
  referenced object after loading it.

Decision:

- Treat `r:<reference id>` scene hints as a request to select that reference in
  the scene.
- Reuse the existing object outline and selection-marker path instead of adding
  a separate marker type.
- For paged exterior scenes, load the hinted cell first and then select the
  target reference once the object is available.

Rationale:

- Reusing normal scene selection gives users a visible marker without creating a
  new editor state model.
- The same behavior works for right-click View, reused scene subviews, and any
  other editor path that routes through scene view hints.

Verification:

- The upstream work-item guard checks that base scene hints select `r:` targets
  and that paged scene hints select the reference after loading its cell.

### Editor: Reusable Script Record Windows

Problem:

- OpenMW issue #3429 tracks a workflow problem in OpenMW-CS: verification and
  search results can open each script record in a separate subview even when a
  script editor is already open and sized for real editing.
- This is especially noisy for script-fix passes because one verifier run can
  produce a long list of script-specific rows.

Decision:

- Extend the existing `Windows/reuse` subview preference to script records.
- Exact subview matches are still reused first.
- If a different script record is requested and a script editor is already open,
  retarget the existing script editor through its normal row-switch path and
  then apply the requested hint.

Rationale:

- Script edits are pushed into the document model through the undo stack as they
  are made, so retargeting the editor does not discard a separate local buffer.
- Reusing the existing preference preserves the option to open multiple script
  windows by disabling subview reuse.
- Applying verifier/search hints after retargeting keeps error navigation
  precise.

Verification:

- The upstream work-item guard checks the script-editor retarget method, the
  `View::addSubView` reuse path, and the preference text that describes the
  script-specific behavior.

### Editor: Reusable Record Editor Windows

Problem:

- OpenMW issue #8391 tracks record edit subviews piling up when `Windows >
  Reuse Subviews` is enabled.
- Opening two Birthsign records, for example, should keep the collection table
  and one retargeted Birthsign editor instead of creating a separate editor for
  every clicked record.

Decision:

- Let `DialogueSubView` retarget itself by record ID and optional hint.
- Reuse an existing dialogue subview when the requested record editor belongs to
  the same record type.
- Keep referenceable records grouped by the shared Referenceable editor family,
  matching the existing factory/model layout.

Rationale:

- The existing record editor already has safe row-switch logic through
  `switchToRow`.
- The reuse preference remains opt-in and exact subview matches are still
  handled before type-family retargeting.
- Fewer record editor windows makes OpenMW-CS safer for large CommunityMP admin
  data review passes.

Verification:

- The upstream work-item guard checks the dialogue retarget method, the reuse
  family helper, the `View::addSubView` retarget path, and the updated reuse
  preference text.

### Editor: Legacy-Encoded Saved Text

Problem:

- OpenMW issue #7451 tracks OpenMW-CS allowing Unicode text to be saved as raw
  UTF-8 bytes in legacy-encoded plugin fields.
- On the next load, those same bytes are decoded through the selected legacy
  code page, turning unsupported characters into mojibake and making edited
  records appear corrupted.

Decision:

- Keep representable characters on the existing UTF-8-to-legacy code-page
  tables.
- Replace complete but unrepresentable UTF-8 characters with `?` before
  writing legacy content, instead of preserving the raw UTF-8 byte sequence.
- Keep incomplete UTF-8 truncation behavior unchanged.
- Pad fixed-size ESM string subrecords from the encoded legacy byte count, not
  the original UTF-8 byte count.

Rationale:

- Legacy Morrowind content cannot round-trip characters outside the selected
  code page. A stable replacement byte is safer than saving bytes that will be
  reinterpreted as unrelated legacy glyphs later.
- Correct fixed-field padding keeps non-ASCII rank/name-style fields from
  becoming shorter after conversion.

Verification:

- `components-tests` covers the OpenMW-CS failure shape with the reported
  four-byte emoji sequence, unsupported three-byte UTF-8, existing incomplete
  UTF-8 truncation, writer-level fallback encoding, and fixed-size writer
  padding after legacy conversion.
- The upstream work-item guard checks the converter fallback, writer padding,
  and regression tests.

### Editor: Masterless Addon Creation

Problem:

- Creating an `.omwaddon` without any selected master can inject or override
  default records in surprising ways.

Decision:

- Keep the advanced workflow possible, but show a warning when the selected
  master list is empty.
- Require explicit confirmation before creating the addon.

Rationale:

- Some developers may intentionally create dependency-free addons.
- The default UX should make the dangerous case obvious before it creates a
  broken content file.

### Editor: Door Teleport Cell Completion

Problem:

- OpenMW issue #7438 tracks OpenMW-CS suggesting exterior cell IDs in the door
  `Teleport Cell` field even though door teleport destinations use an empty
  string for exterior targets.
- In CommunityMP administration work this can produce misleading content edits:
  server operators may autocomplete an exterior ID that looks valid in the
  editor but is not the representation expected by the game data.

Decision:

- Add an interior-only cell display type for ID completion.
- Keep normal cell completion unchanged for pathgrids, dialogue conditions,
  reference creators, and other fields that legitimately accept exterior cells.
- Use the interior-only display type only for door teleport destination cells.

Rationale:

- The fix narrows suggestions without changing the serialized field format or
  blocking manual expert edits.
- Filtering at the completer layer preserves the existing cell table model and
  avoids changing shared cell records or drag/drop behavior.

Verification:

- `openmw-cs-tests` checks that the interior-cell display type is registered as
  an ID completion type.
- The upstream work-item guard checks the filtered completer, the door teleport
  column display type, drag/drop ID mapping, and cell-name length handling.

### Physics and Scripting: MWScript Fall Command

Problem:

- OpenMW issue #8631 tracks the legacy MWScript `Fall` instruction behaving as
  a no-op for swimmers, flying creatures, levitating actors, and the player.
- Legacy mod notes describe `Fall` as a small physics nudge that can bring
  actors down even when normal movement state would otherwise keep them flying
  or swimming.

Decision:

- Implement `Fall` through the world/physics layer instead of mutating active
  magic effects.
- Add a short-lived forced-fall state to physics actors.
- While forced-fall is active, treat the actor as non-flying for movement
  solving, bypass water-walking collision, and apply a downward inertial nudge.
- Clear the forced-fall state once the actor reaches ground or when queued
  movement state is globally cleared.

Rationale:

- The command should affect movement state without silently dispelling
  Levitate, Water Walking, or creature traits.
- Treating the state as physics-only keeps multiplayer actor sync predictable:
  clients will converge through ordinary position/flying/movement packets
  instead of diverging over local spell-list edits.

Verification:

- The upstream work-item guard checks compiler registration, opcode execution,
  world API routing, actor forced-fall state, water-walking bypass, and solver
  behavior for `OpenMW #8631`.
- Runtime follow-up should cover a levitating player, a flying creature, and a
  water-walking actor in a local server session.

Multiplayer note:

- Packet formats are unchanged. This is a local simulation behavior fix that
  should naturally flow through existing movement replication.

## Engineering Principles

- Prefer upstream fixes when they are narrow, tested, and do not assume a
  singleplayer-only runtime.
- Keep multiplayer packet and script compatibility stable unless the fix directly
  addresses a network-state bug.
- Add guard scripts for upstream issue backports so future sync work does not
  silently drop them.
- For physics and rendering, favor deterministic behavior across clients over
  local-only shortcuts.
- For editor work, prevent crashes and data-loss workflows before expanding UI
  conveniences.

## Roadmap

### Immediate Release Candidate Gates

- Re-run focused equipment sync tests with a live local server.
- Build `communitymp`, `openmw`, `tes3mp-server`, `communitymp-hub`,
  `masterserver`, and `openmw-cs` in `RelWithDebInfo`; the public runtime
  executable names are `communitymp.exe`, `communitymp-client.exe`, and
  `communitymp-server.exe`.
- Run the upstream work-item guard script.
- Run TES3MP compatibility guard scripts for account/login, master browser,
  movement, containers, journal/topic sync, and character persistence.
- Run the minimal install smoke with local master server.

### Short-Term Upstream Backports

- Continue 0.52 milestone GUI/rendering fixes that are isolated to the client:
  minimap behavior, non-shader water ripple behavior, and shader-lighting
  corrections where the upstream MR is small and CI-backed.
- Continue editor stability work around stale report rows, destructive record
  actions, and unsafe content creation flows.
- Continue physics fixes that normalize NIF collision data before runtime Bullet
  use.

### Runtime Launching: CommunityMP Executable Names

Decision:

- Use `communitymp` as the public mode launcher.
- Ship the multiplayer client as `communitymp-client`.
- Ship the dedicated server as `communitymp-server`.
- Keep the existing CMake target names for now so upstream sync and developer
  build commands stay stable.

Rationale:

- Users and packaged builds should no longer launch `tes3mp.exe` or
  `tes3mp-server.exe`.
- Hub, browser, shortcuts, and scripts should prefer `communitymp --client` or
  `communitymp --server` so duplicate same-mode launches can be blocked while
  one client and one dedicated server may run side by side.
- The legacy `tes3mp` Lua namespace, config filenames, resource names, and save
  compatibility contracts remain intentionally unchanged until each area has a
  tested CommunityMP-owned replacement or compatibility facade.
- A true single-process client/server merge remains a larger refactor because
  the current dedicated server is a separate server runtime rather than the
  OpenMW client running a built-in headless mode.

### Physics: Scripted SetPos Actor Collision

Problem:

- OpenMW issue #7570 tracks actors losing collision after repeated `SetPos`
  calls.
- `SetPos` changes one axis at a time and is commonly used by mods, admin
  scripts, and multiplayer correction logic to move actors authoritatively.
- Routing actor `SetPos` through the incremental physics-offset path can leave
  the actor collision object dependent on the next simulation pass rather than
  the final scripted position.

Decision:

- Treat actor `SetPos` as an authoritative reposition.
- Route actor `SetPos` through `World::moveObject` with physics sync enabled.
- Keep non-actor `SetPos` and incremental `Move`/`MoveWorld` behavior unchanged.

Rationale:

- `SetPos` is an absolute coordinate write, not a per-frame movement request.
- Immediate actor physics sync clears pending offsets, resets the simulation
  position, and updates the Bullet broadphase AABB in the same frame.
- Keeping `Move`/`MoveWorld` on the offset path preserves moving-platform and
  elevator-style scripts.

Verification:

- The upstream work-item guard checks that actor `SetPos` uses the hard-sync
  `moveObject` path and that actor physics position updates immediately refresh
  the broadphase AABB.

### Multiplayer-Focused Hardening

- Add an equipment convergence test that covers equip, unequip, server resync,
  reconnect, and inventory save/load.
- Add packet timing diagnostics for state packets that are known to race local UI
  actions: equipment, inventory, dynamic stats, position, and actor animation.
- Keep server Lua compatibility checks broad enough to catch legacy TES3MP script
  assumptions around `Players[pid].data`, loaded cells, record stores, custom
  events, timers, and GUI callbacks.

### Multiplayer Combat Authority: Cast And Actor Attack Acceptance

Problem:

- Spell casts were sequenced and remote clients were prevented from applying
  side effects during visual replay, but dedicated-server cast packets still
  moved from client input to fanout without entering the server simulation layer.
- Cast packets do not currently serialize a resolved hit/damage amount, and the
  dedicated server does not yet host the full OpenMW world/magic store needed to
  evaluate spell records with complete singleplayer parity.

Decision:

- Route player and actor cast packets through `ServerSimulation` before fanout.
- Route actor attack packets through `ServerSimulation` before fanout or damage
  application.
- Accept cast starts and failed releases as animation state only.
- For successful regular casts and item-magic casts, require explicit player
  targets to resolve to a live player in the same simulation cell and explicit
  actor targets to resolve to a server-known actor in the relevant loaded cell.
- For actor attacks and targeted actor casts, require the server's stored actor
  state to have a `COMBAT` AI package targeting the same live player or actor.
  The actor combat sequence is accepted only after this server AI-target check,
  so rejected observations cannot burn the canonical sequence number.
- For accepted unarmed melee hits, apply Morrowind-style fatigue-first damage on
  the server. Player targets use full creature stats and switch to health damage
  when knocked down or already out of fatigue. Cached actor targets use their
  authoritative fatigue value and switch to health damage once fatigue is
  already zero or lower.

Rationale:

- This removes another client-owned authority bypass: a client can no longer
  broadcast a successful cast at an arbitrary player or NPC that the server
  cannot resolve.
- Loaded clients can still report observed NPC combat, but the server no longer
  treats the reporting client's cell authority as proof that the NPC should hit,
  cast at, or damage a target.
- Full damage/effect math remains a separate headless-runtime milestone because
  pretending to compute spell effects without the authoritative spell records,
  resistances, reflection, absorption, and scripted side effects would make the
  simulation less correct, not more correct.

Verification:

- `check-tes3mp-combat-sync.ps1` now guards that server cast processors enter
  `ServerSimulation` and that actor cast lists are filtered by server-owned
  target validation before broadcast.
- `check-tes3mp-combat-sync.ps1` and
  `check-tes3mp-actor-movement-ai-sync.ps1` now guard that actor attacks and
  actor casts enter `ServerSimulation`, validate against the stored combat AI
  target, normalize movement sidecars, and accept combat sequences only after
  the server-owned target check succeeds.
- `check-tes3mp-combat-sync.ps1` guards the unarmed melee fatigue-first path for
  players and cached actors, including server-side dynamic-stat broadcast after
  accepted damage.

### Editor and Tooling Roadmap

- Add stale-reference handling to report rows so users can distinguish "fixed but
  report not refreshed" from "still broken".
- Add safer destructive-action affordances for references opened through
  secondary views.
- Improve OpenMW-CS addon creation messaging around masters, dependencies, and
  expected content scope.
- Keep CommunityMP Hub admin editor support focused on server operator workflows:
  save/config/world XML editing, backups, validation, and searchable document
  navigation.

### Packaging Roadmap

- Commit and push before producing distributable packages so every artifact maps
  to a repository commit.
- Keep `communitymp-hub.exe` in the normal install and smoke-test paths.
- Produce test packages only after Release targets pass and the install smoke
  verifies Hub, master query, and direct server query behavior.

## Current Risk Register

- Broad dirty worktree: the upstream-sync branch contains many active changes.
  Commits should group related behavior and avoid mixing generated or local-only
  artifacts.
- Rendering fixes: visual issues need screenshot or runtime checks when possible.
  Compile success is not enough for shader/ripple/minimap behavior.
- Physics fixes: asset-specific bugs need fixture coverage because many NIFs use
  unusual transforms and extra data.
- Multiplayer sync: a fix that is harmless in singleplayer can still reorder
  state across server/client boundaries. Packet channel, packet timing, and
  server Lua persistence all need explicit attention.

## Release Checklist

- Guard scripts pass.
- Focused component tests pass.
- `openmw` and `openmw-cs` build.
- TES3MP server/client smoke passes.
- Hub target builds.
- Install smoke passes with local master server.
- Commit exists on the remote branch before any packaged artifact is shared.
