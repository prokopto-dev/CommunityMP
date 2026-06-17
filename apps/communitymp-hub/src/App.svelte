<script lang="ts">
  import { onMount } from "svelte";
  import { currentMonitor, getCurrentWindow, LogicalSize } from "@tauri-apps/api/window";
  import AdminEditor from "./AdminEditor.svelte";
  import {
    buildLaunchPlan,
    defaultMasterEndpoint,
    queryServerDetails,
    queryServers,
    type LaunchPlan,
    type MasterEndpoint,
    type ServerSummary
  } from "./lib/master";

  type HubTab = "all" | "favorites" | "history" | "mods" | "admin";
  type SortMode = "players" | "name" | "version" | "recent";
  type ModManagerMode = "openmw" | "vortex" | "mod-organizer" | "manual";

  const favoritesKey = "communitymp.hub.favorites";
  const historyKey = "communitymp.hub.history";
  const serverCacheKey = "communitymp.hub.servers";
  const accountNameKey = "communitymp.hub.accountName";
  const modProfileKey = "communitymp.hub.modProfile";
  const legacyFavoritesKey = "communitymp.browser.favorites";
  const legacyHistoryKey = "communitymp.browser.history";
  const legacyServerCacheKey = "communitymp.browser.servers";
  const legacyAccountNameKey = "communitymp.browser.accountName";
  const legacyModProfileKey = "communitymp.browser.modProfile";
  const modManagerModes: ModManagerMode[] = ["openmw", "vortex", "mod-organizer", "manual"];
  const minWindowWidth = 920;
  const minWindowHeight = 620;
  const maxWindowWidth = 1720;
  const maxWindowHeight = 1120;
  const windowDragBlockSelector = [
    "button",
    "a",
    "input",
    "select",
    "textarea",
    "label",
    "code",
    "[data-no-drag]",
    ".source-switch",
    ".stats",
    ".status",
    ".hub-grid",
    ".admin-editor",
    ".control-panel",
    ".filters",
    ".mode-grid",
    ".compat-summary"
  ].join(",");

  let endpoint: MasterEndpoint = {
    host: "master.communitymp.com",
    port: 25560,
    restPort: 8080,
    website: "https://communitymp.com"
  };
  let servers: ServerSummary[] = [];
  let savedServers: Record<string, ServerSummary> = {};
  let favoriteAddresses: string[] = [];
  let historyAddresses: string[] = [];
  let selectedAddress = "";
  let selectedServer: ServerSummary | undefined;
  let visibleServers: ServerSummary[] = [];
  let mergedServers: ServerSummary[] = [];
  let activeTab: HubTab = "all";
  let sortMode: SortMode = "players";
  let search = "";
  let manualAddress = "";
  let hideFull = false;
  let hidePassworded = false;
  let showConnectionControls = false;
  let isRefreshing = false;
  let status = "Ready";
  let source = "";
  let warnings: string[] = [];
  let accountName = "";
  let serverPassword = "";
  let launchPlan: LaunchPlan | undefined;
  let modManagerMode: ModManagerMode = "openmw";
  let modProfileName = "";
  let modDataDirectories = "";
  let modContentFiles = "";
  let modStrictCheck = true;
  let modDataDirectoryList: string[] = [];
  let modContentFileList: string[] = [];
  let modSetupSummary = "";
  let isWindowMaximized = false;
  let appFrame: HTMLElement | undefined;

  $: mergedServers = mergeServers(servers, savedServers);
  $: visibleServers = computeVisibleServers();
  $: selectedServer = selectedAddress
    ? mergedServers.find((server) => server.address === selectedAddress)
    : undefined;
  $: favoriteCount = favoriteAddresses.length;
  $: historyCount = historyAddresses.length;
  $: listedPlayers = servers.reduce((total, server) => total + server.players, 0);
  $: savedServerCount = Object.keys(savedServers).length;
  $: modDataDirectoryList = splitLines(modDataDirectories);
  $: modContentFileList = splitLines(modContentFiles);
  $: modSetupSummary = summarizeModSetup();

  onMount(() => {
    const detachAdaptiveLayout = attachAdaptiveLayout();
    const onResize = () => {
      void refreshMaximizedState();
    };
    const onContextMenu = (event: MouseEvent) => {
      event.preventDefault();
    };

    window.addEventListener("resize", onResize);
    document.addEventListener("contextmenu", onContextMenu);

    void (async () => {
      await fitWindowToMonitor();
      await refreshMaximizedState();
      loadLocalState();
      endpoint = await defaultMasterEndpoint();
    })();

    return () => {
      detachAdaptiveLayout?.();
      window.removeEventListener("resize", onResize);
      document.removeEventListener("contextmenu", onContextMenu);
    };
  });

  async function refresh() {
    isRefreshing = true;
    launchPlan = undefined;
    status = `Querying ${endpoint.host}:${endpoint.restPort}`;
    warnings = [];

    try {
      const result = await queryServers(endpoint);
      servers = result.servers;
      source = result.source;
      warnings = result.warnings;
      status = result.servers.length === 1 ? "1 server listed" : `${result.servers.length} servers listed`;

      for (const server of result.servers) {
        savedServers[server.address] = server;
      }
      savedServers = { ...savedServers };
      saveLocalState();

      if (!selectedAddress && result.servers.length > 0) {
        selectedAddress = result.servers[0].address;
      }
    } catch (error) {
      status = error instanceof Error ? error.message : String(error);
    } finally {
      isRefreshing = false;
    }
  }

  async function refreshSelectedDetails() {
    if (!selectedServer) {
      return;
    }

    status = `Refreshing ${selectedServer.address}`;
    launchPlan = undefined;

    try {
      const detail = await queryServerDetails(endpoint, selectedServer.address);
      savedServers = { ...savedServers, [detail.address]: detail };
      servers = servers.map((server) => (server.address === detail.address ? detail : server));
      selectedAddress = detail.address;
      status = `Updated ${detail.name}`;
      saveLocalState();
    } catch (error) {
      status = error instanceof Error ? error.message : String(error);
    }
  }

  async function prepareLaunch() {
    if (!selectedServer) {
      status = "Select a server first.";
      return;
    }

    try {
      launchPlan = await buildLaunchPlan({
        address: selectedServer.address,
        accountName,
        serverPassword,
        modManagerMode,
        modProfileName: modProfileName.trim(),
        dataDirectories: modDataDirectoryList,
        contentFiles: modContentFileList
      });
      localStorage.setItem(accountNameKey, accountName.trim());
      saveModProfile();
      recordHistory(selectedServer);
      status = `Launch plan ready for ${selectedServer.name}`;
    } catch (error) {
      launchPlan = undefined;
      status = error instanceof Error ? error.message : String(error);
    }
  }

  function selectTab(tab: HubTab) {
    activeTab = tab;
    launchPlan = undefined;
  }

  function selectServer(server: ServerSummary) {
    selectedAddress = server.address;
    launchPlan = undefined;
    serverPassword = "";
    recordHistory(server);
  }

  function toggleFavorite(server: ServerSummary) {
    const exists = favoriteAddresses.includes(server.address);
    favoriteAddresses = exists
      ? favoriteAddresses.filter((address) => address !== server.address)
      : [server.address, ...favoriteAddresses];
    savedServers = { ...savedServers, [server.address]: server };
    saveLocalState();
  }

  function addManualServer() {
    const summary = summarizeManualServer(manualAddress);
    if (!summary) {
      status = "Enter a server address first.";
      return;
    }

    savedServers = { ...savedServers, [summary.address]: summary };
    servers = mergeServers([summary], servers);
    selectedAddress = summary.address;
    manualAddress = "";
    status = `Added ${summary.address}`;
    recordHistory(summary);
  }

  function recordHistory(server: ServerSummary) {
    historyAddresses = [server.address, ...historyAddresses.filter((address) => address !== server.address)].slice(0, 12);
    savedServers = { ...savedServers, [server.address]: server };
    saveLocalState();
  }

  function computeVisibleServers() {
    let list = mergedServers;

    if (activeTab === "favorites") {
      list = list.filter((server) => favoriteAddresses.includes(server.address));
    } else if (activeTab === "history") {
      list = historyAddresses
        .map((address) => list.find((server) => server.address === address))
        .filter((server): server is ServerSummary => Boolean(server));
    }

    const query = search.trim().toLowerCase();
    if (query) {
      list = list.filter((server) =>
        [server.name, server.address, server.version, server.gameMode].some((value) => value.toLowerCase().includes(query))
      );
    }

    if (hideFull) {
      list = list.filter((server) => server.maxPlayers === 0 || server.players < server.maxPlayers);
    }

    if (hidePassworded) {
      list = list.filter((server) => !server.passworded);
    }

    return [...list].sort(compareServers);
  }

  function compareServers(left: ServerSummary, right: ServerSummary) {
    if (sortMode === "name") {
      return left.name.localeCompare(right.name);
    }
    if (sortMode === "version") {
      return right.version.localeCompare(left.version) || left.name.localeCompare(right.name);
    }
    if (sortMode === "recent") {
      return (left.lastUpdateSeconds ?? 0) - (right.lastUpdateSeconds ?? 0);
    }
    return right.players - left.players || left.name.localeCompare(right.name);
  }

  function mergeServers(primary: ServerSummary[], secondary: Record<string, ServerSummary> | ServerSummary[]) {
    const map = new Map<string, ServerSummary>();
    for (const server of Array.isArray(secondary) ? secondary : Object.values(secondary)) {
      map.set(server.address, server);
    }
    for (const server of primary) {
      map.set(server.address, server);
    }
    return [...map.values()];
  }

  function summarizeManualServer(address: string): ServerSummary | undefined {
    const normalized = normalizeManualAddress(address);
    if (!normalized) {
      return undefined;
    }

    const separator = normalized.lastIndexOf(":");
    return {
      address: normalized,
      host: normalized.slice(0, separator),
      port: Number(normalized.slice(separator + 1)),
      name: "Direct server",
      version: "",
      gameMode: "Manual",
      players: 0,
      maxPlayers: 0,
      passworded: false,
      source: "manual"
    };
  }

  function normalizeManualAddress(address: string) {
    const trimmed = address.trim();
    if (!trimmed) {
      return "";
    }
    if (trimmed.startsWith("[") && trimmed.includes("]:")) {
      return trimmed;
    }
    if (trimmed.split(":").length > 2) {
      return `[${trimmed}]:25565`;
    }
    if (trimmed.includes(":")) {
      return trimmed;
    }
    return `${trimmed}:25565`;
  }

  function loadLocalState() {
    migrateLocalStateKeys();
    favoriteAddresses = readJson<string[]>(favoritesKey, []);
    historyAddresses = readJson<string[]>(historyKey, []);
    savedServers = readJson<Record<string, ServerSummary>>(serverCacheKey, {});
    accountName = localStorage.getItem(accountNameKey) ?? "";
    loadModProfile();
  }

  function migrateLocalStateKeys() {
    migrateLocalStorageKey(favoritesKey, legacyFavoritesKey);
    migrateLocalStorageKey(historyKey, legacyHistoryKey);
    migrateLocalStorageKey(serverCacheKey, legacyServerCacheKey);
    migrateLocalStorageKey(accountNameKey, legacyAccountNameKey);
    migrateLocalStorageKey(modProfileKey, legacyModProfileKey);
  }

  function migrateLocalStorageKey(currentKey: string, legacyKey: string) {
    if (localStorage.getItem(currentKey) !== null) {
      return;
    }

    const legacyValue = localStorage.getItem(legacyKey);
    if (legacyValue !== null) {
      localStorage.setItem(currentKey, legacyValue);
    }
  }

  function saveLocalState() {
    localStorage.setItem(favoritesKey, JSON.stringify(favoriteAddresses));
    localStorage.setItem(historyKey, JSON.stringify(historyAddresses));
    localStorage.setItem(serverCacheKey, JSON.stringify(savedServers));
  }

  function loadModProfile() {
    const profile = readJson<{
      mode?: ModManagerMode;
      profileName?: string;
      dataDirectories?: string;
      contentFiles?: string;
      strictCheck?: boolean;
    }>(modProfileKey, {});

    if (profile.mode === "openmw" || profile.mode === "vortex" || profile.mode === "mod-organizer" || profile.mode === "manual") {
      modManagerMode = profile.mode;
    }
    modProfileName = profile.profileName ?? "";
    modDataDirectories = profile.dataDirectories ?? "";
    modContentFiles = profile.contentFiles ?? "";
    modStrictCheck = profile.strictCheck ?? true;
  }

  function saveModProfile() {
    localStorage.setItem(modProfileKey, JSON.stringify({
      mode: modManagerMode,
      profileName: modProfileName,
      dataDirectories: modDataDirectories,
      contentFiles: modContentFiles,
      strictCheck: modStrictCheck
    }));
  }

  function saveModProfileSoon() {
    window.setTimeout(saveModProfile, 0);
  }

  function setModManagerMode(mode: ModManagerMode) {
    modManagerMode = mode;
    saveModProfile();
  }

  function readJson<T>(key: string, fallback: T): T {
    const raw = localStorage.getItem(key);
    if (!raw) {
      return fallback;
    }
    try {
      return JSON.parse(raw) as T;
    } catch {
      return fallback;
    }
  }

  function splitLines(value: string) {
    return value
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean);
  }

  function summarizeModSetup() {
    const total = modDataDirectoryList.length + modContentFileList.length;
    if (total === 0) {
      return "Vanilla launch arguments";
    }
    const dataLabel = modDataDirectoryList.length === 1 ? "1 data path" : `${modDataDirectoryList.length} data paths`;
    const contentLabel = modContentFileList.length === 1 ? "1 content file" : `${modContentFileList.length} content files`;
    return `${managerLabel(modManagerMode)} - ${dataLabel}, ${contentLabel}`;
  }

  function playerLabel(server: ServerSummary) {
    return server.maxPlayers > 0 ? `${server.players}/${server.maxPlayers}` : `${server.players}`;
  }

  function activityLabel(server: ServerSummary) {
    if (server.lastUpdateSeconds === undefined) {
      return "cached";
    }
    if (server.lastUpdateSeconds <= 1) {
      return "just now";
    }
    return `${server.lastUpdateSeconds}s ago`;
  }

  function tabCount(tab: HubTab) {
    if (tab === "favorites") {
      return favoriteCount;
    }
    if (tab === "history") {
      return historyCount;
    }
    if (tab === "mods") {
      return modDataDirectoryList.length + modContentFileList.length;
    }
    if (tab === "admin") {
      return 0;
    }
    return mergedServers.length;
  }

  function tabNote(tab: HubTab) {
    if (tab === "favorites") {
      return "Saved";
    }
    if (tab === "history") {
      return "Recent";
    }
    if (tab === "mods") {
      return modSetupSummary;
    }
    if (tab === "admin") {
      return "Saves";
    }
    return servers.length > 0 ? `${listedPlayers} players` : "Master list";
  }

  function managerLabel(mode: ModManagerMode) {
    if (mode === "vortex") {
      return "Vortex/Nexus";
    }
    if (mode === "mod-organizer") {
      return "Mod Organizer";
    }
    if (mode === "manual") {
      return "Manual export";
    }
    return "OpenMW profile";
  }

  function clampNumber(value: number, min: number, max: number) {
    return Math.min(max, Math.max(min, value));
  }

  function easeOutCubic(value: number) {
    const clamped = clampNumber(value, 0, 1);
    return 1 - Math.pow(1 - clamped, 3);
  }

  function harmonicMean(left: number, right: number) {
    if (left <= 0 || right <= 0) {
      return 0;
    }
    return (2 * left * right) / (left + right);
  }

  function attachAdaptiveLayout() {
    if (!appFrame) {
      return undefined;
    }

    const observer = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) {
        return;
      }

      applyAdaptiveLayout(entry.contentRect.width, entry.contentRect.height);
    });

    observer.observe(appFrame);
    const bounds = appFrame.getBoundingClientRect();
    applyAdaptiveLayout(bounds.width, bounds.height);

    return () => observer.disconnect();
  }

  function applyAdaptiveLayout(width: number, height: number) {
    if (!appFrame || width <= 0 || height <= 0) {
      return;
    }

    const widthScore = clampNumber((width - 720) / 780, 0, 1);
    const heightScore = clampNumber((height - 560) / 440, 0, 1);
    const usableScore = easeOutCubic(harmonicMean(widthScore, heightScore));
    const areaScore = clampNumber(Math.sqrt((width * height) / (1280 * 820)), 0.72, 1.2);
    const density = clampNumber(0.82 + usableScore * 0.24 + (areaScore - 1) * 0.08, 0.82, 1.08);
    const compact = width < 1040 || height < 730;
    const expanded = width >= 1440 && height >= 900;
    const tabColumns = width < 760 ? 1 : width < 1040 ? 2 : 5;
    const adminTabColumns = width < 760 ? 2 : width < 1080 ? 3 : 6;
    const adminMetricColumns = width < 760 ? 1 : width < 980 ? 2 : width < 1320 ? 3 : 5;
    const sidebarWidth = Math.round(clampNumber(width * (expanded ? 0.28 : 0.31), 320, 470));
    const framePadding = Math.round(clampNumber(12 + usableScore * 17, 12, 30));
    const frameGap = Math.round(clampNumber(8 + usableScore * 8, 8, 16));
    const titlebarHeight = Math.round(clampNumber(height * (compact ? 0.11 : 0.14), 72, 132));
    const sealSize = Math.round(clampNumber(52 + usableScore * 34, compact ? 48 : 56, expanded ? 92 : 82));
    const titleSize = Math.round(clampNumber(38 + usableScore * 34, compact ? 34 : 40, expanded ? 78 : 68));
    const headingSize = Math.round(clampNumber(22 + usableScore * 10, 22, 34));

    appFrame.dataset.layout = compact ? "compact" : expanded ? "expanded" : "balanced";
    appFrame.style.setProperty("--hub-density", density.toFixed(3));
    appFrame.style.setProperty("--hub-frame-padding", `${framePadding}px`);
    appFrame.style.setProperty("--hub-frame-gap", `${frameGap}px`);
    appFrame.style.setProperty("--hub-titlebar-height", `${titlebarHeight}px`);
    appFrame.style.setProperty("--hub-sidebar-width", `${sidebarWidth}px`);
    appFrame.style.setProperty("--hub-tab-columns", `${tabColumns}`);
    appFrame.style.setProperty("--hub-admin-tab-columns", `${adminTabColumns}`);
    appFrame.style.setProperty("--hub-admin-metric-columns", `${adminMetricColumns}`);
    appFrame.style.setProperty("--hub-seal-size", `${sealSize}px`);
    appFrame.style.setProperty("--hub-seal-font-size", `${Math.round(sealSize * 0.66)}px`);
    appFrame.style.setProperty("--hub-h1-size", `${titleSize}px`);
    appFrame.style.setProperty("--hub-h2-size", `${headingSize}px`);
  }

  function logicalWorkAreaSize(monitor: Awaited<ReturnType<typeof currentMonitor>>) {
    if (!monitor) {
      return null;
    }

    const scale = monitor.scaleFactor || 1;
    const area = monitor.workArea?.size ?? monitor.size;

    return {
      width: area.width / scale,
      height: area.height / scale
    };
  }

  async function fitWindowToMonitor() {
    try {
      const monitor = await currentMonitor();
      const workArea = logicalWorkAreaSize(monitor);
      if (!workArea) {
        return;
      }

      const appWindow = getCurrentWindow();
      const minWidth = Math.min(minWindowWidth, Math.max(720, workArea.width - 24));
      const minHeight = Math.min(minWindowHeight, Math.max(560, workArea.height - 24));
      const maxWidth = Math.max(minWidth, Math.min(maxWindowWidth, workArea.width - 16));
      const maxHeight = Math.max(minHeight, Math.min(maxWindowHeight, workArea.height - 36));
      const targetWidth = Math.round(clampNumber(workArea.width * 0.9, minWidth, maxWidth));
      const targetHeight = Math.round(clampNumber(workArea.height * 0.88, minHeight, maxHeight));

      await appWindow.setSizeConstraints({
        minWidth: Math.round(minWidth),
        minHeight: Math.round(minHeight)
      });
      await appWindow.setSize(new LogicalSize(targetWidth, targetHeight));
      await appWindow.center();
    } catch (error) {
      console.warn("Failed to fit CommunityMP Hub window to monitor", error);
    }
  }

  async function refreshMaximizedState() {
    try {
      isWindowMaximized = await getCurrentWindow().isMaximized();
    } catch {
      isWindowMaximized = false;
    }
  }

  async function minimizeWindow() {
    try {
      await getCurrentWindow().minimize();
    } catch (error) {
      console.warn("Failed to minimize CommunityMP Hub window", error);
    }
  }

  async function toggleMaximizeWindow() {
    try {
      await getCurrentWindow().toggleMaximize();
      window.setTimeout(() => {
        void refreshMaximizedState();
      }, 80);
    } catch (error) {
      console.warn("Failed to toggle CommunityMP Hub window maximize state", error);
    }
  }

  async function closeWindow() {
    try {
      await getCurrentWindow().close();
    } catch (error) {
      console.warn("Failed to close CommunityMP Hub window", error);
    }
  }

  async function startWindowDrag(event: MouseEvent | PointerEvent) {
    if (event.button !== 0) {
      return;
    }

    if ((event.target as HTMLElement | null)?.closest(windowDragBlockSelector)) {
      return;
    }

    try {
      event.preventDefault();
      await getCurrentWindow().startDragging();
    } catch (error) {
      console.warn("Failed to drag CommunityMP Hub window", error);
    }
  }
</script>

<main class="shell" class:maximized={isWindowMaximized}>
  <div bind:this={appFrame} class="app-frame" class:admin-mode={activeTab === "admin"} data-tauri-drag-region role="presentation" onmousedown={startWindowDrag}>
    <div class="titlebar" aria-label="CommunityMP Hub" data-tauri-drag-region role="presentation">
      <div class="brand" data-tauri-drag-region>
        <div class="brand-seal" aria-hidden="true" data-tauri-drag-region>C</div>
        <div data-tauri-drag-region>
          <p class="eyebrow">CommunityMP 0.1.0</p>
          <h1>{activeTab === "admin" ? "Admin Editor" : "Server Directory"}</h1>
          <p class="summary">
            {activeTab === "admin"
              ? "Authoritative save, config, player, and world-state operations for server hosts."
              : "Server discovery, account-safe launch previews, and local mod setup handoff."}
          </p>
        </div>
      </div>
      <div class="hero-actions" data-no-drag>
        <div class="window-controls" aria-label="Window controls">
          <button
            class="window-button"
            type="button"
            aria-label="Minimize"
            title="Minimize"
            onclick={(event) => {
              event.stopPropagation();
              void minimizeWindow();
            }}
          >-</button>
          <button
            class="window-button"
            type="button"
            aria-label="Maximize"
            title="Maximize"
            onclick={(event) => {
              event.stopPropagation();
              void toggleMaximizeWindow();
            }}
          >□</button>
          <button
            class="window-button close"
            type="button"
            aria-label="Close"
            title="Close"
            onclick={(event) => {
              event.stopPropagation();
              void closeWindow();
            }}
          >x</button>
        </div>
        <a class="site" href={endpoint.website}>{endpoint.website}</a>
        {#if activeTab !== "admin"}
          <button class="primary" disabled={isRefreshing} onclick={refresh}>
            {isRefreshing ? "Refreshing" : "Refresh Master"}
          </button>
        {/if}
      </div>
    </div>

    <section class="source-switch" aria-label="Workspace">
      <button class:source-selected={activeTab === "all"} onclick={() => selectTab("all")}>
        <span>All Servers</span>
        <small>{tabCount("all")} - {tabNote("all")}</small>
      </button>
      <button class:source-selected={activeTab === "favorites"} onclick={() => selectTab("favorites")}>
        <span>Favorites</span>
        <small>{tabCount("favorites")} - {tabNote("favorites")}</small>
      </button>
      <button class:source-selected={activeTab === "history"} onclick={() => selectTab("history")}>
        <span>Recent</span>
        <small>{tabCount("history")} - {tabNote("history")}</small>
      </button>
      <button class:source-selected={activeTab === "mods"} onclick={() => selectTab("mods")}>
        <span>Mod Setup</span>
        <small>{tabNote("mods")}</small>
      </button>
      <button class:source-selected={activeTab === "admin"} onclick={() => selectTab("admin")}>
        <span>Admin Editor</span>
        <small>Save tools</small>
      </button>
    </section>

    {#if activeTab !== "admin"}
      <section class="stats" aria-label="Hub summary">
        <article>
          <span>{servers.length}</span>
          <p>Listed Servers</p>
        </article>
        <article>
          <span>{listedPlayers}</span>
          <p>Listed Players</p>
        </article>
        <article>
          <span>{savedServerCount}</span>
          <p>Known Servers</p>
        </article>
        <article>
          <span>{modDataDirectoryList.length + modContentFileList.length}</span>
          <p>Mod Args</p>
        </article>
      </section>
    {/if}

    {#if activeTab !== "admin"}
      <section class="status" class:warning={warnings.length > 0}>
        <strong>{status}</strong>
        {#if source}
          <span>{source}</span>
        {/if}
        {#if warnings.length > 0}
          <small>{warnings.length} server entr{warnings.length === 1 ? "y" : "ies"} had invalid master data.</small>
        {/if}
      </section>
    {/if}

    {#if activeTab === "admin"}
      <AdminEditor />
    {:else}
    <section class="hub-grid">
      <div class="list-panel">
        {#if activeTab === "mods"}
          <div class="panel-header">
            <div>
              <p class="eyebrow">Mod Manager Compatibility</p>
              <h2>Launch Argument Profile</h2>
            </div>
            <button class="ghost" onclick={saveModProfile}>Save Profile</button>
          </div>

          <div class="mode-grid" aria-label="Mod setup source">
            {#each modManagerModes as mode}
              <button class:active={modManagerMode === mode} onclick={() => setModManagerMode(mode)}>
                {managerLabel(mode)}
              </button>
            {/each}
          </div>

          <div class="mod-form">
            <label>
              Profile label
              <input bind:value={modProfileName} oninput={saveModProfileSoon} placeholder="Example: Tamriel Rebuilt co-op" />
            </label>
            <label>
              Data folders
              <textarea
                bind:value={modDataDirectories}
                oninput={saveModProfileSoon}
                spellcheck="false"
                placeholder="One folder per line, for example:&#10;C:\Games\OpenMWMods\Tamriel_Data&#10;C:\Games\OpenMWMods\TR_Mainland"
              ></textarea>
            </label>
            <label>
              Content files
              <textarea
                bind:value={modContentFiles}
                oninput={saveModProfileSoon}
                spellcheck="false"
                placeholder="One content file per line, for example:&#10;Morrowind.esm&#10;Tribunal.esm&#10;Bloodmoon.esm&#10;Tamriel_Data.esm"
              ></textarea>
            </label>
            <label class="check">
              <input type="checkbox" bind:checked={modStrictCheck} onchange={saveModProfile} />
              Keep this profile visible before launch so players can compare server requirements.
            </label>
          </div>

          <div class="compat-summary">
            <article>
              <span>{managerLabel(modManagerMode)}</span>
              <p>Source mode</p>
            </article>
            <article>
              <span>{modDataDirectoryList.length}</span>
              <p>Data paths</p>
            </article>
            <article>
              <span>{modContentFileList.length}</span>
              <p>Content files</p>
            </article>
          </div>

          <div class="hint-box">
            <h3>How this helps</h3>
            <p>
              Paste an exported mod manager loadout here and the launch preview will include OpenMW-compatible
              `--data` and `--content` arguments. It does not store account passwords or convert old scripts.
            </p>
          </div>
        {:else}
          <div class="panel-header">
            <div>
              <p class="eyebrow">{activeTab === "all" ? "Master List" : activeTab === "favorites" ? "Saved List" : "History"}</p>
              <h2>{activeTab === "all" ? "Browse Servers" : activeTab === "favorites" ? "Favorite Servers" : "Recent Servers"}</h2>
            </div>
            <button class="ghost" onclick={() => (showConnectionControls = !showConnectionControls)}>
              {showConnectionControls ? "Hide Connection" : "Connection"}
            </button>
          </div>

          {#if showConnectionControls}
            <section class="control-panel">
              <label>
                Master host
                <input bind:value={endpoint.host} />
              </label>
              <label>
                GNS port
                <input class="number" type="number" min="1" max="65535" bind:value={endpoint.port} />
              </label>
              <label>
                REST port
                <input class="number" type="number" min="1" max="65535" bind:value={endpoint.restPort} />
              </label>
              <label class="manual">
                Direct server
                <span>
                  <input bind:value={manualAddress} placeholder="127.0.0.1:25565" />
                  <button onclick={addManualServer}>Add</button>
                </span>
              </label>
            </section>
          {/if}

          <div class="filters">
            <input bind:value={search} placeholder="Search name, address, version, or mode" />
            <select bind:value={sortMode} aria-label="Sort servers">
              <option value="players">Players</option>
              <option value="name">Name</option>
              <option value="version">Version</option>
              <option value="recent">Last update</option>
            </select>
            <label class="check"><input type="checkbox" bind:checked={hideFull} /> Hide full</label>
            <label class="check"><input type="checkbox" bind:checked={hidePassworded} /> Hide passworded</label>
          </div>

          <div class="server-list" aria-label="Server list">
            {#if visibleServers.length === 0}
              <div class="empty">No servers match this view.</div>
            {:else}
              {#each visibleServers as server (server.address)}
                <article class="server-card" class:selected={selectedAddress === server.address}>
                  <button
                    type="button"
                    class="star"
                    class:active={favoriteAddresses.includes(server.address)}
                    title="Toggle favorite"
                    onclick={() => toggleFavorite(server)}
                  >
                    Favorite
                  </button>
                  <button type="button" class="server-select" onclick={() => selectServer(server)}>
                    <div class="server-main">
                      <h3>{server.name}</h3>
                      <p>{server.address}</p>
                    </div>
                    <div class="server-meta">
                      <span>{playerLabel(server)}</span>
                      <span>{server.version || "unknown"}</span>
                      <span>{server.passworded ? "Password" : "Open"}</span>
                    </div>
                  </button>
                </article>
              {/each}
            {/if}
          </div>
        {/if}
      </div>

      <aside class="details-panel">
        {#if selectedServer}
          <div class="details-header">
            <div>
              <p class="eyebrow">Selected Server</p>
              <h2>{selectedServer.name}</h2>
              <p>{selectedServer.address}</p>
            </div>
            <button class="ghost" onclick={refreshSelectedDetails}>Update</button>
          </div>

          <dl class="details">
            <div><dt>Players</dt><dd>{playerLabel(selectedServer)}</dd></div>
            <div><dt>Version</dt><dd>{selectedServer.version || "unknown"}</dd></div>
            <div><dt>Mode</dt><dd>{selectedServer.gameMode || "unknown"}</dd></div>
            <div><dt>Access</dt><dd>{selectedServer.passworded ? "Server password required" : "Open"}</dd></div>
            <div><dt>Updated</dt><dd>{activityLabel(selectedServer)}</dd></div>
            <div><dt>Mod setup</dt><dd>{modSetupSummary}</dd></div>
          </dl>

          <div class="join-box">
            <label>
              Account username
              <input bind:value={accountName} placeholder="Your server account" />
            </label>
            {#if selectedServer.passworded}
              <label>
                Server join password
                <input type="password" bind:value={serverPassword} placeholder="Connection password only" />
              </label>
            {/if}
            <button class="primary" onclick={prepareLaunch}>Prepare Launch</button>
            <p class="note">Character names are loaded after account login. Account passwords are never stored here.</p>
          </div>

          {#if launchPlan}
            <div class="launch-plan">
              <p class="eyebrow">Launch Preview</p>
              <code>{launchPlan.displayCommand}</code>
              {#each launchPlan.warnings as warning}
                <small>{warning}</small>
              {/each}
            </div>
          {/if}
        {:else}
          <div class="empty details-empty">Select or add a server to see details and prepare a launch.</div>
        {/if}
      </aside>
    </section>
    {/if}
  </div>
</main>
