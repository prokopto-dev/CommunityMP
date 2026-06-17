<script lang="ts">
  import {
    loadAdminWorkspace,
    readAdminDocument,
    saveAdminDocument,
    saveAdminStructuredDocument,
    saveAdminState,
    type AccountSummary,
    type AdminWorkspace,
    type DataFileRequirement,
    type DocumentSummary,
    type SaveDocument,
    type WorldEntrySummary
  } from "./lib/admin";
  import XmlDocumentEditor from "./XmlDocumentEditor.svelte";

  type AdminTab = "overview" | "security" | "data-files" | "players" | "world" | "documents";
  type DocumentMode = "fields" | "source";

  const adminPathKey = "communitymp.admin.dataPath";
  const adminWorldLimitKey = "communitymp.admin.worldLimit";
  const adminPreviewRowsKey = "communitymp.admin.previewRows";

  let activeAdminTab: AdminTab = "overview";
  let workspace: AdminWorkspace | undefined;
  let dataPath = "";
  let status = "Open a CommunityMP server data folder.";
  let isLoading = false;
  let isSaving = false;
  let banNameDraft = "";
  let banIpDraft = "";
  let banNamesText = "";
  let banIpsText = "";
  let dataFiles: DataFileRequirement[] = [];
  let accountSearch = "";
  let worldSearch = "";
  let sourceSearch = "";
  let selectedDocumentPath = "";
  let sourceContent = "";
  let sourceStatus = "Select a document.";
  let sourceDirty = false;
  let structuredDocument: SaveDocument | undefined;
  let structuredDirty = false;
  let documentParseError = "";
  let documentMode: DocumentMode = "fields";
  let worldLimit = 80;
  let previewRows = 18;

  $: accountCount = workspace?.accounts.length ?? 0;
  $: characterCount = workspace?.accounts.reduce((total, account) => total + account.characterCount, 0) ?? 0;
  $: documentCount = workspace?.documents.length ?? 0;
  $: cellCount = workspace?.world.cells.length ?? 0;
  $: recordStoreCount = workspace?.world.recordStores.length ?? 0;
  $: filteredAccounts = filterAccounts(workspace?.accounts ?? [], accountSearch);
  $: filteredCells = limitWorldEntries(filterWorldEntries(workspace?.world.cells ?? [], worldSearch), worldLimit);
  $: filteredRecordStores = limitWorldEntries(filterWorldEntries(workspace?.world.recordStores ?? [], worldSearch), worldLimit);
  $: filteredDocuments = filterDocuments(workspace?.documents ?? [], sourceSearch);
  $: worldManifestPath = workspace?.world.manifest?.path ?? "";
  $: worldCorePath = workspace?.world.core?.path ?? "";
  $: worldGlobalPath = workspace?.world.global?.path ?? "";

  function loadLocalAdminState() {
    dataPath = localStorage.getItem(adminPathKey) ?? defaultDataPath();
    worldLimit = readNumber(adminWorldLimitKey, 80);
    previewRows = readNumber(adminPreviewRowsKey, 18);
  }

  loadLocalAdminState();

  async function loadWorkspace() {
    isLoading = true;
    sourceContent = "";
    sourceDirty = false;
    structuredDocument = undefined;
    structuredDirty = false;
    documentParseError = "";
    selectedDocumentPath = "";
    status = "Loading admin workspace...";

    try {
      const loaded = await loadAdminWorkspace(dataPath);
      workspace = loaded;
      dataPath = loaded.dataPath;
      localStorage.setItem(adminPathKey, dataPath);
      hydrateEditableState(loaded);
      status = `Loaded ${loaded.dataPath}`;
      activeAdminTab = "overview";
    } catch (error) {
      workspace = undefined;
      status = error instanceof Error ? error.message : String(error);
    } finally {
      isLoading = false;
    }
  }

  function hydrateEditableState(loaded: AdminWorkspace) {
    banNamesText = loaded.server.banList.playerNames.join("\n");
    banIpsText = loaded.server.banList.ipAddresses.join("\n");
    dataFiles = loaded.server.dataFiles.requirements.map((requirement) => ({
      name: requirement.name,
      checksums: [...requirement.checksums]
    }));
  }

  async function saveStructuredChanges() {
    if (!workspace) {
      status = "Load a workspace before saving.";
      return;
    }

    isSaving = true;
    status = "Saving admin XML...";

    try {
      const result = await saveAdminState(
        workspace.dataPath,
        {
          playerNames: splitLines(banNamesText),
          ipAddresses: splitLines(banIpsText)
        },
        dataFiles
      );
      status = `Saved ${result.savedPaths.join(", ")}`;
      if (result.backupPaths.length > 0) {
        status += ` with ${result.backupPaths.length} backup file${result.backupPaths.length === 1 ? "" : "s"}`;
      }
      const reloaded = await loadAdminWorkspace(workspace.dataPath);
      workspace = reloaded;
      hydrateEditableState(reloaded);
    } catch (error) {
      status = error instanceof Error ? error.message : String(error);
    } finally {
      isSaving = false;
    }
  }

  async function saveCurrentChanges() {
    if (activeAdminTab === "documents") {
      if (documentMode === "fields") {
        await saveStructuredDocument();
      } else {
        await saveSourceDocument();
      }
      return;
    }

    await saveStructuredChanges();
  }

  function addBanName() {
    const next = banNameDraft.trim();
    if (!next) {
      return;
    }
    banNamesText = [...splitLines(banNamesText), next].join("\n");
    banNameDraft = "";
  }

  function addBanIp() {
    const next = banIpDraft.trim();
    if (!next) {
      return;
    }
    banIpsText = [...splitLines(banIpsText), next].join("\n");
    banIpDraft = "";
  }

  function addDataFile() {
    dataFiles = [...dataFiles, { name: "NewPlugin.esp", checksums: [] }];
  }

  function removeDataFile(index: number) {
    dataFiles = dataFiles.filter((_, itemIndex) => itemIndex !== index);
  }

  function updateDataFileName(index: number, value: string) {
    dataFiles = dataFiles.map((requirement, itemIndex) =>
      itemIndex === index ? { ...requirement, name: value } : requirement
    );
  }

  function updateDataFileChecksums(index: number, value: string) {
    dataFiles = dataFiles.map((requirement, itemIndex) =>
      itemIndex === index ? { ...requirement, checksums: splitLines(value) } : requirement
    );
  }

  async function openDocument(path: string) {
    if (!workspace) {
      return;
    }

    activeAdminTab = "documents";
    selectedDocumentPath = path;
    sourceStatus = `Loading ${path}`;
    sourceDirty = false;
    structuredDirty = false;
    structuredDocument = undefined;
    documentParseError = "";

    try {
      const document = await readAdminDocument(workspace.dataPath, path);
      sourceContent = document.content;
      structuredDocument = document.structured;
      documentParseError = document.parseError ?? "";
      documentMode = document.structured ? "fields" : "source";
      sourceStatus = `${document.path} - ${formatBytes(document.sizeBytes)}`;
    } catch (error) {
      sourceContent = "";
      structuredDocument = undefined;
      documentParseError = "";
      sourceStatus = error instanceof Error ? error.message : String(error);
    }
  }

  async function refreshOpenDocument() {
    if (!workspace || !selectedDocumentPath) {
      return;
    }

    const previousMode = documentMode;
    await openDocument(selectedDocumentPath);
    if (previousMode === "source" || structuredDocument) {
      documentMode = previousMode;
    }
  }

  async function saveSourceDocument() {
    if (!workspace || !selectedDocumentPath) {
      sourceStatus = "Select a document before saving.";
      return;
    }

    isSaving = true;
    sourceStatus = "Validating and saving XML...";

    try {
      const result = await saveAdminDocument(workspace.dataPath, selectedDocumentPath, sourceContent);
      sourceDirty = false;
      sourceStatus = `Saved ${result.savedPaths[0]}${result.backupPaths.length > 0 ? " with backup" : ""}`;
      workspace = await loadAdminWorkspace(workspace.dataPath);
      await refreshOpenDocument();
    } catch (error) {
      sourceStatus = error instanceof Error ? error.message : String(error);
    } finally {
      isSaving = false;
    }
  }

  async function saveStructuredDocument() {
    if (!workspace || !selectedDocumentPath || !structuredDocument) {
      sourceStatus = "Open a structured CommunityMP XML document before saving fields.";
      return;
    }

    isSaving = true;
    sourceStatus = "Validating and saving fields...";

    try {
      const result = await saveAdminStructuredDocument(workspace.dataPath, selectedDocumentPath, structuredDocument);
      structuredDirty = false;
      sourceDirty = false;
      sourceStatus = `Saved ${result.savedPaths[0]}${result.backupPaths.length > 0 ? " with backup" : ""}`;
      workspace = await loadAdminWorkspace(workspace.dataPath);
      await refreshOpenDocument();
      documentMode = "fields";
    } catch (error) {
      sourceStatus = error instanceof Error ? error.message : String(error);
    } finally {
      isSaving = false;
    }
  }

  function handleStructuredChange(document: SaveDocument) {
    structuredDocument = document;
    structuredDirty = true;
  }

  function handleSourceChange(content: string) {
    sourceContent = content;
    sourceDirty = true;
  }

  function selectAdminTab(tab: AdminTab) {
    activeAdminTab = tab;
  }

  function setWorldLimit(value: number) {
    worldLimit = value;
    localStorage.setItem(adminWorldLimitKey, String(worldLimit));
  }

  function setPreviewRows(value: number) {
    previewRows = value;
    localStorage.setItem(adminPreviewRowsKey, String(previewRows));
  }

  function defaultDataPath() {
    return "server/data";
  }

  function readNumber(key: string, fallback: number) {
    const raw = Number(localStorage.getItem(key));
    return Number.isFinite(raw) && raw > 0 ? raw : fallback;
  }

  function splitLines(value: string) {
    return value
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean);
  }

  function filterAccounts(accounts: AccountSummary[], query: string) {
    const normalized = query.trim().toLowerCase();
    if (!normalized) {
      return accounts;
    }

    return accounts.filter((account) =>
      [account.account, account.path, ...account.characters.map((character) => `${character.name} ${character.cell}`)]
        .join(" ")
        .toLowerCase()
        .includes(normalized)
    );
  }

  function filterWorldEntries(entries: WorldEntrySummary[], query: string) {
    const normalized = query.trim().toLowerCase();
    if (!normalized) {
      return entries;
    }

    return entries.filter((entry) => [entry.name, entry.path, entry.kind].join(" ").toLowerCase().includes(normalized));
  }

  function limitWorldEntries(entries: WorldEntrySummary[], limit: number) {
    return entries.slice(0, Math.max(10, limit));
  }

  function filterDocuments(documents: DocumentSummary[], query: string) {
    const normalized = query.trim().toLowerCase();
    if (!normalized) {
      return documents;
    }

    return documents.filter((document) =>
      [document.path, document.kind, document.domain].join(" ").toLowerCase().includes(normalized)
    );
  }

  function formatBytes(bytes: number) {
    if (bytes >= 1024 * 1024) {
      return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
    }
    if (bytes >= 1024) {
      return `${(bytes / 1024).toFixed(1)} KB`;
    }
    return `${bytes} B`;
  }

  function formatDate(seconds?: number) {
    if (!seconds) {
      return "unknown";
    }
    return new Date(seconds * 1000).toLocaleString();
  }

  function issueText() {
    if (!workspace || workspace.issues.length === 0) {
      return "No save layout warnings";
    }
    return `${workspace.issues.length} warning${workspace.issues.length === 1 ? "" : "s"}`;
  }
</script>

<section class="admin-editor">
  <header class="admin-command-bar">
    <div class="admin-path">
      <label for="admin-data-path">Server data folder</label>
      <div class="admin-path-row">
        <input id="admin-data-path" bind:value={dataPath} placeholder="C:\CommunityMP\server\data" spellcheck="false" />
        <button class="admin-button" type="button" disabled={isLoading} onclick={loadWorkspace}>
          {isLoading ? "Loading" : "Load"}
        </button>
      </div>
    </div>
    <div class="admin-actions">
      <button
        class="admin-button primary-admin"
        type="button"
        disabled={isSaving || !workspace}
        title="Saves the current admin view. Document saves use the selected Fields or Raw XML mode."
        onclick={saveCurrentChanges}
      >
        {isSaving ? "Saving" : "Save Changes"}
      </button>
    </div>
  </header>

  <nav class="admin-tabs" aria-label="Admin editor sections">
    <button class:active={activeAdminTab === "overview"} onclick={() => selectAdminTab("overview")}>Overview</button>
    <button class:active={activeAdminTab === "security"} onclick={() => selectAdminTab("security")}>Security</button>
    <button class:active={activeAdminTab === "data-files"} onclick={() => selectAdminTab("data-files")}>Data Files</button>
    <button class:active={activeAdminTab === "players"} onclick={() => selectAdminTab("players")}>Players</button>
    <button class:active={activeAdminTab === "world"} onclick={() => selectAdminTab("world")}>World</button>
    <button class:active={activeAdminTab === "documents"} onclick={() => selectAdminTab("documents")}>Documents</button>
  </nav>

  <div class="admin-status" class:warning={workspace && workspace.issues.length > 0}>
    <strong>{status}</strong>
    <span>{issueText()}</span>
  </div>

  <div class="admin-workspace-body" class:documents-mode={activeAdminTab === "documents"}>
  {#if !workspace}
    <div class="admin-empty">
      <h2>Admin Workspace</h2>
      <p>Load `server/data` from a CommunityMP install or development runtime.</p>
    </div>
  {:else if activeAdminTab === "overview"}
    <div class="admin-panel-grid">
      <article class="admin-metric">
        <span>{accountCount}</span>
        <p>Accounts</p>
      </article>
      <article class="admin-metric">
        <span>{characterCount}</span>
        <p>Characters</p>
      </article>
      <article class="admin-metric">
        <span>{cellCount}</span>
        <p>Cells</p>
      </article>
      <article class="admin-metric">
        <span>{recordStoreCount}</span>
        <p>Record Stores</p>
      </article>
      <article class="admin-metric">
        <span>{documentCount}</span>
        <p>XML Documents</p>
      </article>
    </div>

    <div class="admin-columns">
      <section class="admin-panel">
        <div class="admin-panel-title">
          <h2>Layout</h2>
          <span class="admin-tip" title="These are the files the server will read for admin-controlled state.">?</span>
        </div>
        <dl class="admin-definition-list">
          <div><dt>Data root</dt><dd>{workspace.dataPath}</dd></div>
          <div><dt>Banlist</dt><dd>{workspace.server.banList.exists ? workspace.server.banList.path : "missing"}</dd></div>
          <div><dt>Required files</dt><dd>{workspace.server.dataFiles.exists ? workspace.server.dataFiles.path : "missing"}</dd></div>
          <div><dt>World manifest</dt><dd>{workspace.world.manifest?.path ?? "not created yet"}</dd></div>
        </dl>
      </section>

      <section class="admin-panel">
        <div class="admin-panel-title">
          <h2>Editor Preferences</h2>
          <span class="admin-tip" title="These affect only this editor view. They are not written to server saves.">?</span>
        </div>
        <label class="admin-slider">
          World rows
          <input
            type="range"
            min="20"
            max="240"
            step="10"
            value={worldLimit}
            oninput={(event) => setWorldLimit(Number(event.currentTarget.value))}
          />
          <span>{worldLimit}</span>
        </label>
        <label class="admin-slider">
          Raw XML rows
          <input
            type="range"
            min="10"
            max="36"
            step="2"
            value={previewRows}
            oninput={(event) => setPreviewRows(Number(event.currentTarget.value))}
          />
          <span>{previewRows}</span>
        </label>
      </section>
    </div>

    {#if workspace.issues.length > 0}
      <section class="admin-panel">
        <div class="admin-panel-title">
          <h2>Warnings</h2>
          <span class="admin-tip" title="Warnings usually mean missing optional files, old layouts, or XML the editor could not parse.">?</span>
        </div>
        <ul class="admin-issues">
          {#each workspace.issues as issue}
            <li>{issue}</li>
          {/each}
        </ul>
      </section>
    {/if}
  {:else if activeAdminTab === "security"}
    <div class="admin-columns">
      <section class="admin-panel">
        <div class="admin-panel-title">
          <h2>Blocked Accounts</h2>
          <span class="admin-tip" title="One account or character display name per line. Duplicate entries are removed when saved.">?</span>
        </div>
        <div class="admin-inline-add">
          <input bind:value={banNameDraft} placeholder="Player or account name" onkeydown={(event) => event.key === "Enter" && addBanName()} />
          <button type="button" onclick={addBanName}>Add</button>
        </div>
        <textarea bind:value={banNamesText} rows="14" spellcheck="false"></textarea>
      </section>

      <section class="admin-panel">
        <div class="admin-panel-title">
          <h2>Blocked IPs</h2>
          <span class="admin-tip" title="One IP address per line. Use exact addresses; wildcards are not expanded by the editor.">?</span>
        </div>
        <div class="admin-inline-add">
          <input bind:value={banIpDraft} placeholder="127.0.0.1" onkeydown={(event) => event.key === "Enter" && addBanIp()} />
          <button type="button" onclick={addBanIp}>Add</button>
        </div>
        <textarea bind:value={banIpsText} rows="14" spellcheck="false"></textarea>
      </section>
    </div>
  {:else if activeAdminTab === "data-files"}
    <section class="admin-panel">
      <div class="admin-panel-title">
        <h2>Required Data Files</h2>
        <div class="admin-title-actions">
          <span class="admin-tip" title="These plugins and checksums are enforced by the server during client compatibility checks.">?</span>
          <button type="button" onclick={addDataFile}>Add Plugin</button>
        </div>
      </div>
      <div class="admin-data-file-list">
        {#each dataFiles as requirement, index}
          <article class="admin-data-file">
            <label>
              Plugin
              <input
                value={requirement.name}
                spellcheck="false"
                oninput={(event) => updateDataFileName(index, event.currentTarget.value)}
              />
            </label>
            <label>
              Checksums
              <textarea
                rows="3"
                spellcheck="false"
                value={requirement.checksums.join("\n")}
                oninput={(event) => updateDataFileChecksums(index, event.currentTarget.value)}
              ></textarea>
            </label>
            <button class="danger" type="button" onclick={() => removeDataFile(index)}>Remove</button>
          </article>
        {/each}
      </div>
    </section>
  {:else if activeAdminTab === "players"}
    <section class="admin-panel">
      <div class="admin-panel-title">
        <h2>Accounts And Characters</h2>
        <input class="admin-search" bind:value={accountSearch} placeholder="Search accounts, characters, cells" />
      </div>
      <div class="admin-table-scroll">
        <table class="admin-table">
          <thead>
            <tr>
              <th>Account</th>
              <th>Characters</th>
              <th>Modified</th>
              <th>File</th>
            </tr>
          </thead>
          <tbody>
            {#each filteredAccounts as account}
              <tr>
                <td>{account.account}</td>
                <td>{account.characterCount}</td>
                <td>{formatDate(account.modifiedAt)}</td>
                <td><button type="button" onclick={() => openDocument(account.path)}>Open</button></td>
              </tr>
              {#each account.characters as character}
                <tr class="child-row">
                  <td>{character.name}</td>
                  <td>Level {character.level ?? "?"} - {character.cell || "unknown cell"}</td>
                  <td>{formatDate(character.modifiedAt)}</td>
                  <td><button type="button" onclick={() => openDocument(character.path)}>Open</button></td>
                </tr>
              {/each}
            {/each}
          </tbody>
        </table>
      </div>
    </section>
  {:else if activeAdminTab === "world"}
    <section class="admin-panel">
      <div class="admin-panel-title">
        <h2>World State</h2>
        <div class="admin-title-actions">
          <input class="admin-search" bind:value={worldSearch} placeholder="Search cells or record stores" />
          <span class="admin-tip" title="The row limit keeps very large shards responsive. Increase it when you need deeper inspection.">?</span>
        </div>
      </div>
      <div class="admin-world-globals">
        {#if worldManifestPath}<button type="button" onclick={() => openDocument(worldManifestPath)}>Manifest</button>{/if}
        {#if worldCorePath}<button type="button" onclick={() => openDocument(worldCorePath)}>Core</button>{/if}
        {#if worldGlobalPath}<button type="button" onclick={() => openDocument(worldGlobalPath)}>Global</button>{/if}
      </div>

      <h3>Cells</h3>
      <div class="admin-table-scroll compact">
        <table class="admin-table">
          <thead><tr><th>Cell</th><th>Objects</th><th>Size</th><th>File</th></tr></thead>
          <tbody>
            {#each filteredCells as entry}
              <tr>
                <td>{entry.name}</td>
                <td>{entry.objectCount ?? 0}</td>
                <td>{formatBytes(entry.sizeBytes)}</td>
                <td><button type="button" onclick={() => openDocument(entry.path)}>Open</button></td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>

      <h3>Record Stores</h3>
      <div class="admin-table-scroll compact">
        <table class="admin-table">
          <thead><tr><th>Store</th><th>Records</th><th>Size</th><th>File</th></tr></thead>
          <tbody>
            {#each filteredRecordStores as entry}
              <tr>
                <td>{entry.name}</td>
                <td>{entry.recordCount ?? 0}</td>
                <td>{formatBytes(entry.sizeBytes)}</td>
                <td><button type="button" onclick={() => openDocument(entry.path)}>Open</button></td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    </section>
  {:else}
    <section class="document-workspace">
      <aside class="document-list-panel">
        <div class="admin-panel-title">
          <h2>Documents</h2>
          <span class="admin-tip" title="Every indexed XML save can be opened here. Use Fields for normal edits and Raw XML for advanced repair.">?</span>
        </div>
        <input class="admin-search" bind:value={sourceSearch} placeholder="Search path, kind, or domain" />
        <div class="document-list" aria-label="XML document list">
          {#each filteredDocuments as document}
            <button
              type="button"
              class:selected={selectedDocumentPath === document.path}
              onclick={() => openDocument(document.path)}
            >
              <span>{document.path}</span>
              <small>{document.kind} - {document.domain} - {formatBytes(document.sizeBytes)}</small>
            </button>
          {/each}
        </div>
      </aside>

      <section class="document-edit-panel">
        <div class="admin-panel-title">
          <div>
            <h2>{selectedDocumentPath || "Select a Document"}</h2>
            <p class="source-meta">{sourceStatus}{sourceDirty || structuredDirty ? " - unsaved changes" : ""}</p>
          </div>
          <div class="admin-title-actions">
            <button type="button" disabled={!selectedDocumentPath} onclick={refreshOpenDocument}>Reload</button>
            <span class="admin-tip" title="Fields saves rebuild a valid CommunityMP XML document and create a backup. Raw XML saves validate before writing.">?</span>
          </div>
        </div>

        {#if selectedDocumentPath}
          <XmlDocumentEditor
            {structuredDocument}
            {sourceContent}
            {sourceDirty}
            {structuredDirty}
            parseError={documentParseError}
            mode={documentMode}
            {previewRows}
            onStructuredChange={handleStructuredChange}
            onSourceChange={handleSourceChange}
            onModeChange={(mode) => (documentMode = mode)}
            onSaveFields={saveStructuredDocument}
            onSaveSource={saveSourceDocument}
          />
        {:else}
          <div class="admin-empty document-empty">
            <h2>Pick a File</h2>
            <p>Choose an account, character, world, cell, recordstore, or server config XML document from the list.</p>
          </div>
        {/if}
      </section>
    </section>
  {/if}
  </div>
</section>
