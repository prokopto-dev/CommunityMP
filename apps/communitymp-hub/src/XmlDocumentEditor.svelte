<script lang="ts">
  import type { SaveDocument, SaveNode } from "./lib/admin";

  type DocumentMode = "fields" | "source";

  type NodeRow = {
    node: SaveNode;
    path: number[];
    depth: number;
    label: string;
  };

  export let structuredDocument: SaveDocument | undefined;
  export let sourceContent = "";
  export let parseError = "";
  export let sourceDirty = false;
  export let structuredDirty = false;
  export let mode: DocumentMode = "fields";
  export let previewRows = 18;
  export let onStructuredChange: (document: SaveDocument) => void = () => {};
  export let onSourceChange: (content: string) => void = () => {};
  export let onModeChange: (mode: DocumentMode) => void = () => {};
  export let onSaveFields: () => void = () => {};
  export let onSaveSource: () => void = () => {};

  let fieldSearch = "";
  let newAttributeKey = "";
  let newAttributeValue = "";
  let expandedKeys = new Set<string>(["root"]);

  $: attributeEntries = Object.entries(structuredDocument?.attributes ?? {}).sort(([left], [right]) =>
    left.localeCompare(right)
  );
  $: fieldRows = buildRows(structuredDocument?.data);
  $: visibleFieldRows = filterRows(fieldRows, fieldSearch);

  function setMode(next: DocumentMode) {
    mode = next;
    onModeChange(next);
  }

  function buildRows(root: SaveNode | undefined) {
    if (!root) {
      return [];
    }

    return flattenNode(root, [], 0, []);
  }

  function flattenNode(node: SaveNode, path: number[], depth: number, parentKeys: string[]): NodeRow[] {
    const labelParts = [...parentKeys, node.key || "(empty)"];
    const rows: NodeRow[] = [{ node, path, depth, label: labelParts.join(" / ") }];
    const key = pathKey(path);

    if (expandedKeys.has(key) || fieldSearch.trim()) {
      for (let index = 0; index < node.children.length; index += 1) {
        rows.push(...flattenNode(node.children[index], [...path, index], depth + 1, labelParts));
      }
    }

    return rows;
  }

  function filterRows(rows: NodeRow[], query: string) {
    const normalized = query.trim().toLowerCase();
    if (!normalized) {
      return rows;
    }

    return rows.filter((row) =>
      [row.label, row.node.key, row.node.keyType, row.node.valueType, row.node.text]
        .join(" ")
        .toLowerCase()
        .includes(normalized)
    );
  }

  function pathKey(path: number[]) {
    return path.length === 0 ? "root" : path.join(".");
  }

  function collectNodeKeys(node: SaveNode | undefined, path: number[] = []): string[] {
    if (!node) {
      return [];
    }

    return [
      pathKey(path),
      ...node.children.flatMap((child, index) => collectNodeKeys(child, [...path, index]))
    ];
  }

  function getNodeAtPath(node: SaveNode, path: number[]): SaveNode | undefined {
    if (path.length === 0) {
      return node;
    }

    const [nextIndex, ...rest] = path;
    const child = node.children[nextIndex];
    return child ? getNodeAtPath(child, rest) : undefined;
  }

  function isExpanded(path: number[]) {
    return expandedKeys.has(pathKey(path));
  }

  function toggleExpanded(path: number[]) {
    const key = pathKey(path);
    const next = new Set(expandedKeys);
    if (next.has(key)) {
      next.delete(key);
    } else {
      next.add(key);
    }
    expandedKeys = next;
  }

  function expandAll() {
    expandedKeys = new Set(collectNodeKeys(structuredDocument?.data));
  }

  function collapseAll() {
    expandedKeys = new Set(["root"]);
  }

  function updateAttribute(key: string, value: string) {
    if (!structuredDocument) {
      return;
    }

    onStructuredChange({
      ...structuredDocument,
      attributes: {
        ...structuredDocument.attributes,
        [key]: value
      }
    });
  }

  function addAttribute() {
    if (!structuredDocument) {
      return;
    }

    const key = newAttributeKey.trim();
    if (!key) {
      return;
    }

    onStructuredChange({
      ...structuredDocument,
      attributes: {
        ...structuredDocument.attributes,
        [key]: newAttributeValue.trim()
      }
    });
    newAttributeKey = "";
    newAttributeValue = "";
  }

  function removeAttribute(key: string) {
    if (!structuredDocument) {
      return;
    }

    const attributes = { ...structuredDocument.attributes };
    delete attributes[key];
    onStructuredChange({ ...structuredDocument, attributes });
  }

  function updateNode(path: number[], updater: (node: SaveNode) => SaveNode) {
    if (!structuredDocument?.data) {
      return;
    }

    onStructuredChange({
      ...structuredDocument,
      data: updateNodeAtPath(structuredDocument.data, path, updater)
    });
  }

  function updateNodeAtPath(node: SaveNode, path: number[], updater: (node: SaveNode) => SaveNode): SaveNode {
    if (path.length === 0) {
      return updater(node);
    }

    const [nextIndex, ...rest] = path;
    return {
      ...node,
      children: node.children.map((child, index) =>
        index === nextIndex ? updateNodeAtPath(child, rest, updater) : child
      )
    };
  }

  function removeNode(path: number[]) {
    if (!structuredDocument?.data || path.length === 0) {
      return;
    }

    onStructuredChange({
      ...structuredDocument,
      data: removeNodeAtPath(structuredDocument.data, path)
    });
  }

  function removeNodeAtPath(node: SaveNode, path: number[]): SaveNode {
    const [nextIndex, ...rest] = path;
    if (rest.length === 0) {
      return {
        ...node,
        children: node.children.filter((_, index) => index !== nextIndex)
      };
    }

    return {
      ...node,
      children: node.children.map((child, index) => (index === nextIndex ? removeNodeAtPath(child, rest) : child))
    };
  }

  function addChild(path: number[]) {
    if (!structuredDocument?.data) {
      return;
    }

    const parentNode = getNodeAtPath(structuredDocument.data, path);
    const childPath = [...path, parentNode?.children.length ?? 0];

    updateNode(path, (node) => {
      const child = createChildNode(node.children.length + 1);
      return {
        ...node,
        valueType: "table",
        text: "",
        children: [...node.children, child]
      };
    });

    expandedKeys = new Set([...expandedKeys, pathKey(path), pathKey(childPath)]);
  }

  function createChildNode(index: number): SaveNode {
    return {
      key: String(index),
      keyType: "number",
      valueType: "string",
      text: "",
      children: []
    };
  }

  function updateNodeType(path: number[], valueType: string) {
    updateNode(path, (node) => ({
      ...node,
      valueType,
      text: valueType === "table" ? "" : node.text,
      children: valueType === "table" ? node.children : []
    }));
  }

  function canEditValue(node: SaveNode) {
    return node.valueType !== "table" && node.children.length === 0;
  }

  function sourceRows() {
    return Math.max(12, previewRows);
  }
</script>

<div class="xml-editor">
  <div class="xml-editor-toolbar">
    <div class="xml-mode-switch" role="tablist" aria-label="Document editor mode">
      <button class:active={mode === "fields"} type="button" onclick={() => setMode("fields")}>Fields</button>
      <button class:active={mode === "source"} type="button" onclick={() => setMode("source")}>Raw XML</button>
    </div>
    <div class="xml-save-actions">
      {#if mode === "fields"}
        <button type="button" class="primary-admin" disabled={!structuredDocument} onclick={onSaveFields}>
          Save Fields{structuredDirty ? " *" : ""}
        </button>
      {:else}
        <button type="button" class="primary-admin" onclick={onSaveSource}>
          Save XML{sourceDirty ? " *" : ""}
        </button>
      {/if}
    </div>
  </div>

  {#if mode === "fields"}
    {#if structuredDocument?.data}
      <div class="xml-fields">
        <section class="xml-attributes">
          <div class="admin-panel-title">
            <h3>Document Metadata</h3>
            <span class="admin-tip" title="Metadata describes the save domain, document kind, schema, and save timestamp.">?</span>
          </div>
          <div class="xml-attribute-grid">
            {#each attributeEntries as [key, value]}
              <label>
                {key}
                <span>
                  <input value={value} spellcheck="false" oninput={(event) => updateAttribute(key, event.currentTarget.value)} />
                  <button type="button" class="danger" onclick={() => removeAttribute(key)}>Remove</button>
                </span>
              </label>
            {/each}
          </div>
          <div class="xml-add-attribute">
            <input bind:value={newAttributeKey} placeholder="Attribute name" spellcheck="false" />
            <input bind:value={newAttributeValue} placeholder="Value" spellcheck="false" onkeydown={(event) => event.key === "Enter" && addAttribute()} />
            <button type="button" onclick={addAttribute}>Add</button>
          </div>
        </section>

        <section class="xml-node-editor">
          <div class="xml-node-tools">
            <input bind:value={fieldSearch} placeholder="Search fields, values, or paths" />
            <button type="button" onclick={expandAll}>Expand</button>
            <button type="button" onclick={collapseAll}>Collapse</button>
          </div>

          <div class="xml-node-grid" role="table" aria-label="Structured XML fields">
            <div class="xml-node-row header" role="row">
              <span>Field</span>
              <span>Key Type</span>
              <span>Value Type</span>
              <span>Value</span>
              <span>Actions</span>
            </div>
            {#each visibleFieldRows as row (pathKey(row.path))}
              <div class="xml-node-row" role="row" style={`--xml-depth: ${row.depth}`}>
                <div class="xml-node-key">
                  <button
                    type="button"
                    class="xml-expand"
                    disabled={row.node.children.length === 0}
                    title={isExpanded(row.path) ? "Collapse field" : "Expand field"}
                    onclick={() => toggleExpanded(row.path)}
                  >
                    {row.node.children.length === 0 ? "" : isExpanded(row.path) ? "-" : "+"}
                  </button>
                  <input
                    value={row.node.key}
                    disabled={row.path.length === 0}
                    spellcheck="false"
                    oninput={(event) => updateNode(row.path, (node) => ({ ...node, key: event.currentTarget.value }))}
                  />
                </div>
                <select
                  value={row.node.keyType}
                  disabled={row.path.length === 0}
                  onchange={(event) => updateNode(row.path, (node) => ({ ...node, keyType: event.currentTarget.value }))}
                >
                  <option value="string">string</option>
                  <option value="number">number</option>
                </select>
                <select
                  value={row.node.valueType}
                  disabled={row.path.length === 0 || row.node.children.length > 0}
                  title={row.node.children.length > 0 ? "Remove child fields before changing this type." : "Stored value type"}
                  onchange={(event) => updateNodeType(row.path, event.currentTarget.value)}
                >
                  <option value="table">table</option>
                  <option value="string">string</option>
                  <option value="number">number</option>
                  <option value="boolean">boolean</option>
                </select>
                {#if canEditValue(row.node)}
                  {#if row.node.valueType === "boolean"}
                    <select
                      value={row.node.text || "false"}
                      onchange={(event) => updateNode(row.path, (node) => ({ ...node, text: event.currentTarget.value }))}
                    >
                      <option value="false">false</option>
                      <option value="true">true</option>
                    </select>
                  {:else}
                    <input
                      value={row.node.text}
                      spellcheck="false"
                      oninput={(event) => updateNode(row.path, (node) => ({ ...node, text: event.currentTarget.value }))}
                    />
                  {/if}
                {:else}
                  <span class="xml-child-count">{row.node.children.length} child field{row.node.children.length === 1 ? "" : "s"}</span>
                {/if}
                <div class="xml-row-actions">
                  <button type="button" onclick={() => addChild(row.path)}>Add Child</button>
                  <button class="danger" type="button" disabled={row.path.length === 0} onclick={() => removeNode(row.path)}>
                    Remove
                  </button>
                </div>
              </div>
            {/each}
          </div>
        </section>
      </div>
    {:else}
      <div class="xml-empty">
        <h3>Fields Unavailable</h3>
        <p>{parseError || "This file does not have a CommunityMP structured save tree."}</p>
      </div>
    {/if}
  {:else}
    <textarea
      class="source-editor"
      rows={sourceRows()}
      value={sourceContent}
      spellcheck="false"
      oninput={(event) => onSourceChange(event.currentTarget.value)}
    ></textarea>
  {/if}
</div>
