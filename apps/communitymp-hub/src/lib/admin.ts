import { invoke } from "@tauri-apps/api/core";

function hasTauriRuntime() {
  return typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;
}

function requireTauriRuntime<T>(): Promise<T> {
  return Promise.reject(new Error("Admin editing requires the packaged CommunityMP Hub app."));
}

export type BanListDocument = {
  path: string;
  exists: boolean;
  playerNames: string[];
  ipAddresses: string[];
};

export type DataFileRequirement = {
  name: string;
  checksums: string[];
};

export type DataFileDocument = {
  path: string;
  exists: boolean;
  requirements: DataFileRequirement[];
};

export type DocumentSummary = {
  path: string;
  kind: string;
  domain: string;
  schemaVersion?: string;
  savedAt?: string;
  sizeBytes: number;
  modifiedAt?: number;
  editable: boolean;
};

export type WorldEntrySummary = {
  name: string;
  path: string;
  kind: string;
  objectCount?: number;
  recordCount?: number;
  sizeBytes: number;
  modifiedAt?: number;
};

export type WorldAdminState = {
  manifest?: DocumentSummary;
  core?: DocumentSummary;
  global?: DocumentSummary;
  cells: WorldEntrySummary[];
  recordStores: WorldEntrySummary[];
};

export type CharacterSummary = {
  name: string;
  path: string;
  level?: number;
  cell: string;
  sizeBytes: number;
  modifiedAt?: number;
};

export type AccountSummary = {
  account: string;
  path: string;
  characterCount: number;
  characters: CharacterSummary[];
  sizeBytes: number;
  modifiedAt?: number;
};

export type AdminWorkspace = {
  dataPath: string;
  server: {
    banList: BanListDocument;
    dataFiles: DataFileDocument;
  };
  world: WorldAdminState;
  accounts: AccountSummary[];
  documents: DocumentSummary[];
  issues: string[];
};

export type DocumentContent = {
  path: string;
  content: string;
  sizeBytes: number;
  modifiedAt?: number;
  structured?: SaveDocument;
  parseError?: string;
};

export type WriteResult = {
  savedPaths: string[];
  backupPaths: string[];
  warnings: string[];
};

export type EditableBanList = {
  playerNames: string[];
  ipAddresses: string[];
};

export type SaveNode = {
  key: string;
  keyType: "number" | "string" | string;
  valueType: "boolean" | "number" | "string" | "table" | string;
  text: string;
  children: SaveNode[];
};

export type SaveDocument = {
  attributes: Record<string, string>;
  data?: SaveNode;
};

const sampleStructuredDocument: SaveDocument = {
  attributes: {
    domain: "world",
    format: "CommunityMP XML Save",
    kind: "world-core",
    saveSchemaVersion: "2",
    savedAt: "1781450000",
    schemaVersion: "1"
  },
  data: {
    key: "data",
    keyType: "string",
    valueType: "table",
    text: "",
    children: [
      { key: "time", keyType: "string", valueType: "number", text: "10", children: [] },
      { key: "weather", keyType: "string", valueType: "string", text: "clear", children: [] },
      {
        key: "globalVariables",
        keyType: "string",
        valueType: "table",
        text: "",
        children: [
          { key: "chargenState", keyType: "string", valueType: "number", text: "0", children: [] },
          { key: "mainQuestEnabled", keyType: "string", valueType: "boolean", text: "true", children: [] }
        ]
      }
    ]
  }
};

const sampleDocuments: DocumentSummary[] = [
  {
    path: "saves/server/security/banlist.xml",
    kind: "server-banlist",
    domain: "server-security",
    schemaVersion: "1",
    savedAt: "1781450000",
    sizeBytes: 340,
    modifiedAt: 1781450000,
    editable: true
  },
  {
    path: "saves/world/state/core.xml",
    kind: "world-core",
    domain: "world",
    schemaVersion: "2",
    savedAt: "1781450000",
    sizeBytes: 560,
    modifiedAt: 1781450000,
    editable: true
  },
  {
    path: "saves/ACCOUNT001/characters/joe/joe.xml",
    kind: "character",
    domain: "player",
    schemaVersion: "2",
    savedAt: "1781450000",
    sizeBytes: 820,
    modifiedAt: 1781450000,
    editable: true
  },
  {
    path: "saves/world/cells/Balmora/cell.xml",
    kind: "cell",
    domain: "world-cell",
    schemaVersion: "2",
    savedAt: "1781450000",
    sizeBytes: 1440,
    modifiedAt: 1781450000,
    editable: true
  }
];

function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

function sampleWorkspace(dataPath: string): AdminWorkspace {
  return {
    dataPath: dataPath || "server/data",
    server: {
      banList: {
        path: "saves/server/security/banlist.xml",
        exists: true,
        playerNames: ["ExampleTroubleAccount"],
        ipAddresses: ["127.0.0.1"]
      },
      dataFiles: {
        path: "saves/server/config/data-files.xml",
        exists: true,
        requirements: [
          { name: "Morrowind.esm", checksums: ["0x7B6AF5B9"] },
          { name: "Tribunal.esm", checksums: ["0xF481F334"] }
        ]
      }
    },
    world: {
      manifest: sampleDocuments[1],
      core: sampleDocuments[1],
      global: undefined,
      cells: [{ name: "Balmora", path: "saves/world/cells/Balmora/cell.xml", kind: "cell", objectCount: 42, sizeBytes: 1440 }],
      recordStores: [
        {
          name: "npc",
          path: "saves/world/recordstores/npc/records.xml",
          kind: "recordstore",
          recordCount: 12,
          sizeBytes: 940
        }
      ]
    },
    accounts: [
      {
        account: "ACCOUNT001",
        path: "saves/ACCOUNT001/account.xml",
        characterCount: 1,
        sizeBytes: 420,
        modifiedAt: 1781450000,
        characters: [
          {
            name: "joe",
            path: "saves/ACCOUNT001/characters/joe/joe.xml",
            level: 7,
            cell: "Balmora",
            sizeBytes: 820,
            modifiedAt: 1781450000
          }
        ]
      }
    ],
    documents: clone(sampleDocuments),
    issues: ["Browser preview uses sample data. Packaged CommunityMP Hub edits real server files."]
  };
}

function sampleDocument(path: string): DocumentContent {
  return {
    path,
    content:
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<save domain=\"world\" format=\"CommunityMP XML Save\" kind=\"world-core\" saveSchemaVersion=\"2\" savedAt=\"1781450000\" schemaVersion=\"1\">\n    <node key=\"data\" keyType=\"string\" type=\"table\">\n        <node key=\"time\" keyType=\"string\" type=\"number\">10</node>\n        <node key=\"weather\" keyType=\"string\" type=\"string\">clear</node>\n    </node>\n</save>\n",
    sizeBytes: 560,
    modifiedAt: 1781450000,
    structured: clone(sampleStructuredDocument)
  };
}

export function loadAdminWorkspace(dataPath: string): Promise<AdminWorkspace> {
  if (!hasTauriRuntime()) {
    return Promise.resolve(sampleWorkspace(dataPath));
  }

  return invoke("load_admin_workspace", { dataPath, data_path: dataPath });
}

export function readAdminDocument(dataPath: string, relativePath: string): Promise<DocumentContent> {
  if (!hasTauriRuntime()) {
    return Promise.resolve(sampleDocument(relativePath));
  }

  return invoke("read_admin_document", {
    dataPath,
    data_path: dataPath,
    relativePath,
    relative_path: relativePath
  });
}

export function saveAdminState(
  dataPath: string,
  banList: EditableBanList,
  dataFiles: DataFileRequirement[]
): Promise<WriteResult> {
  if (!hasTauriRuntime()) {
    return Promise.resolve({ savedPaths: ["saves/server/security/banlist.xml", "saves/server/config/data-files.xml"], backupPaths: [], warnings: [] });
  }

  return invoke("save_admin_state", {
    request: {
      dataPath,
      data_path: dataPath,
      banList,
      ban_list: banList,
      dataFiles,
      data_files: dataFiles
    }
  });
}

export function saveAdminDocument(dataPath: string, relativePath: string, content: string): Promise<WriteResult> {
  if (!hasTauriRuntime()) {
    return Promise.resolve({ savedPaths: [relativePath], backupPaths: [`${relativePath}.bak`], warnings: [] });
  }

  return invoke("save_admin_document", {
    request: {
      dataPath,
      data_path: dataPath,
      relativePath,
      relative_path: relativePath,
      content
    }
  });
}

export function saveAdminStructuredDocument(
  dataPath: string,
  relativePath: string,
  document: SaveDocument
): Promise<WriteResult> {
  if (!hasTauriRuntime()) {
    return Promise.resolve({ savedPaths: [relativePath], backupPaths: [`${relativePath}.bak`], warnings: [] });
  }

  return invoke("save_admin_structured_document", {
    request: {
      dataPath,
      data_path: dataPath,
      relativePath,
      relative_path: relativePath,
      document
    }
  });
}
