import { invoke } from "@tauri-apps/api/core";

const fallbackEndpoint: MasterEndpoint = {
  host: "master.communitymp.com",
  port: 25560,
  restPort: 8080,
  website: "https://communitymp.com"
};

function hasTauriRuntime() {
  return typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;
}

export type MasterEndpoint = {
  host: string;
  port: number;
  restPort: number;
  website: string;
};

export type ServerSummary = {
  address: string;
  host: string;
  port: number;
  name: string;
  version: string;
  gameMode: string;
  players: number;
  maxPlayers: number;
  passworded: boolean;
  lastUpdateSeconds?: number;
  source: string;
};

export type MasterQueryResult = {
  endpoint: MasterEndpoint;
  servers: ServerSummary[];
  source: string;
  warnings: string[];
};

export type LaunchRequest = {
  address: string;
  accountName: string;
  serverPassword?: string;
  modManagerMode?: string;
  modProfileName?: string;
  dataDirectories?: string[];
  contentFiles?: string[];
};

export type LaunchPlan = {
  executable: string;
  arguments: string[];
  displayCommand: string;
  warnings: string[];
};

export function defaultMasterEndpoint(): Promise<MasterEndpoint> {
  if (!hasTauriRuntime()) {
    return Promise.resolve(fallbackEndpoint);
  }

  return invoke("default_master_endpoint");
}

export function queryServers(endpoint: MasterEndpoint): Promise<MasterQueryResult> {
  if (!hasTauriRuntime()) {
    return Promise.resolve({
      endpoint,
      servers: [],
      source: "Browser preview",
      warnings: ["CommunityMP Hub native commands are available in the packaged Tauri app."]
    });
  }

  return invoke("query_servers", {
    host: endpoint.host,
    port: endpoint.port,
    restPort: endpoint.restPort,
    rest_port: endpoint.restPort
  });
}

export function queryServerDetails(endpoint: MasterEndpoint, address: string): Promise<ServerSummary> {
  if (!hasTauriRuntime()) {
    return Promise.reject(new Error(`Native server detail query is unavailable in browser preview for ${address}.`));
  }

  return invoke("query_server_details", {
    host: endpoint.host,
    restPort: endpoint.restPort,
    rest_port: endpoint.restPort,
    address
  });
}

export function buildLaunchPlan(request: LaunchRequest): Promise<LaunchPlan> {
  if (!hasTauriRuntime()) {
    const args = ["--client", `--connect=${request.address}`, `--name=${request.accountName.trim()}`];
    if (request.serverPassword) {
      args.push("--password=<server-password>");
    }
    for (const data of request.dataDirectories ?? []) {
      args.push(`--data=${data}`);
    }
    for (const content of request.contentFiles ?? []) {
      args.push(`--content=${content}`);
    }

    return Promise.resolve({
      executable: "communitymp",
      arguments: args,
      displayCommand: ["communitymp", ...args].join(" "),
      warnings: ["Browser preview generated this locally; packaged Hub uses the native command backend."]
    });
  }

  return invoke("build_launch_plan", { request });
}
