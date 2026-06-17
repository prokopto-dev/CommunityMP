use serde::Serialize;
use serde_json::Value;
use std::time::Duration;

const DEFAULT_MASTER_HOST: &str = "master.communitymp.com";
const DEFAULT_MASTER_PORT: u16 = 25560;
const DEFAULT_MASTER_REST_PORT: u16 = 8080;
const DEFAULT_WEBSITE: &str = "https://communitymp.com";

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MasterEndpoint {
    pub host: String,
    pub port: u16,
    pub rest_port: u16,
    pub website: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ServerSummary {
    pub address: String,
    pub host: String,
    pub port: u16,
    pub name: String,
    pub version: String,
    pub game_mode: String,
    pub players: u16,
    pub max_players: u16,
    pub passworded: bool,
    pub last_update_seconds: Option<u64>,
    pub source: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MasterQueryResult {
    pub endpoint: MasterEndpoint,
    pub servers: Vec<ServerSummary>,
    pub source: String,
    pub warnings: Vec<String>,
}

#[tauri::command]
pub fn default_master_endpoint() -> MasterEndpoint {
    MasterEndpoint {
        host: DEFAULT_MASTER_HOST.to_owned(),
        port: DEFAULT_MASTER_PORT,
        rest_port: DEFAULT_MASTER_REST_PORT,
        website: DEFAULT_WEBSITE.to_owned(),
    }
}

#[tauri::command]
pub async fn query_servers(
    host: String,
    port: u16,
    rest_port: u16,
) -> Result<MasterQueryResult, String> {
    query_servers_impl(host, port, rest_port).await
}

pub async fn query_servers_impl(
    host: String,
    port: u16,
    rest_port: u16,
) -> Result<MasterQueryResult, String> {
    let endpoint = MasterEndpoint {
        host: normalize_host_label(&host)?,
        port,
        rest_port,
        website: DEFAULT_WEBSITE.to_owned(),
    };
    let url = format!("{}/api/servers", rest_base_url(&endpoint.host, rest_port)?);
    let payload = fetch_json(&url).await?;
    let list = payload
        .get("list servers")
        .and_then(Value::as_object)
        .ok_or_else(|| "Master response did not contain a server list.".to_owned())?;

    let mut warnings = Vec::new();
    let mut servers = Vec::new();

    for (address, entry) in list {
        match parse_server_summary(address, entry, "master-rest") {
            Ok(server) => servers.push(server),
            Err(error) => warnings.push(format!("{address}: {error}")),
        }
    }

    servers.sort_by(|left, right| {
        right
            .players
            .cmp(&left.players)
            .then_with(|| left.name.to_lowercase().cmp(&right.name.to_lowercase()))
    });

    Ok(MasterQueryResult {
        endpoint,
        servers,
        source: url,
        warnings,
    })
}

#[tauri::command]
pub async fn query_server_details(
    host: String,
    rest_port: u16,
    address: String,
) -> Result<ServerSummary, String> {
    query_server_details_impl(host, rest_port, address).await
}

pub async fn query_server_details_impl(
    host: String,
    rest_port: u16,
    address: String,
) -> Result<ServerSummary, String> {
    let host = normalize_host_label(&host)?;
    let address = normalize_address_label(&address)?;
    let url = format!(
        "{}/api/servers/{}",
        rest_base_url(&host, rest_port)?,
        address
    );
    let payload = match fetch_json(&url).await {
        Ok(payload) => payload,
        Err(detail_error) => {
            return query_server_details_from_list(host, rest_port, address, detail_error).await;
        }
    };
    let server = payload
        .get("server")
        .ok_or_else(|| "Master response did not contain server details.".to_owned())?;

    parse_server_summary(&address, server, "master-rest-detail")
}

async fn query_server_details_from_list(
    host: String,
    rest_port: u16,
    address: String,
    detail_error: String,
) -> Result<ServerSummary, String> {
    let result = query_servers_impl(host, DEFAULT_MASTER_PORT, rest_port).await?;
    result
        .servers
        .into_iter()
        .find(|server| server.address == address)
        .ok_or_else(|| {
            format!(
                "Could not refresh server details from REST detail endpoint or list fallback: {detail_error}"
            )
        })
}

async fn fetch_json(url: &str) -> Result<Value, String> {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(4))
        .build()
        .map_err(|error| format!("Could not create HTTP client: {error}"))?;
    let response = client
        .get(url)
        .send()
        .await
        .map_err(|error| format!("Could not query master REST endpoint: {error}"))?;

    if !response.status().is_success() {
        return Err(format!(
            "Master REST endpoint returned HTTP {}.",
            response.status()
        ));
    }

    response
        .json::<Value>()
        .await
        .map_err(|error| format!("Could not parse master REST response: {error}"))
}

fn parse_server_summary(
    address: &str,
    value: &Value,
    source: &str,
) -> Result<ServerSummary, String> {
    let (host, port) = split_server_address(address)?;
    let players = number_field(value, "players").unwrap_or(0);
    let max_players = number_field(value, "max_players").unwrap_or(0);

    Ok(ServerSummary {
        address: format!("{host}:{port}"),
        host,
        port,
        name: string_field(value, "hostname").unwrap_or_else(|| "Unnamed server".to_owned()),
        version: string_field(value, "version").unwrap_or_default(),
        game_mode: string_field(value, "modname").unwrap_or_default(),
        players,
        max_players,
        passworded: value.get("passw").and_then(Value::as_bool).unwrap_or(false),
        last_update_seconds: value.get("last_update").and_then(Value::as_u64),
        source: source.to_owned(),
    })
}

fn string_field(value: &Value, name: &str) -> Option<String> {
    value
        .get(name)
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|text| !text.is_empty())
        .map(ToOwned::to_owned)
}

fn number_field(value: &Value, name: &str) -> Option<u16> {
    let raw = value.get(name)?.as_u64()?;
    Some(raw.min(u16::MAX as u64) as u16)
}

fn rest_base_url(host: &str, rest_port: u16) -> Result<String, String> {
    let host = host_for_url(host)?;
    Ok(format!("http://{host}:{rest_port}"))
}

fn host_for_url(host: &str) -> Result<String, String> {
    let host = normalize_host_label(host)?;
    if host.starts_with('[') {
        return Ok(host);
    }
    if host.matches(':').count() > 1 {
        return Ok(format!("[{host}]"));
    }
    Ok(host)
}

fn normalize_host_label(host: &str) -> Result<String, String> {
    let trimmed = host.trim();
    if trimmed.is_empty() {
        return Err("Master host is required.".to_owned());
    }

    let without_scheme = trimmed
        .strip_prefix("http://")
        .or_else(|| trimmed.strip_prefix("https://"))
        .unwrap_or(trimmed);
    let without_path = without_scheme.split('/').next().unwrap_or(without_scheme);

    if without_path.starts_with('[') {
        if let Some(end) = without_path.find(']') {
            return Ok(without_path[..=end].to_owned());
        }
    }

    if without_path.matches(':').count() == 1 {
        let mut parts = without_path.splitn(2, ':');
        let host_part = parts.next().unwrap_or_default();
        let port_part = parts.next().unwrap_or_default();
        if !host_part.is_empty() && port_part.parse::<u16>().is_ok() {
            return Ok(host_part.to_owned());
        }
    }

    Ok(without_path.to_owned())
}

fn normalize_address_label(address: &str) -> Result<String, String> {
    let address = address.trim();
    if address.is_empty() {
        return Err("Server address is required.".to_owned());
    }
    let (host, port) = split_server_address(address)?;
    Ok(format!("{host}:{port}"))
}

fn split_server_address(address: &str) -> Result<(String, u16), String> {
    let address = address.trim();
    if address.is_empty() {
        return Err("Server address is required.".to_owned());
    }

    if let Some(end) = address.find("]:") {
        let host = address[..=end].trim_matches(&['[', ']'][..]).to_owned();
        let port = parse_port(&address[end + 2..])?;
        return Ok((host, port));
    }

    let separator = address
        .rfind(':')
        .ok_or_else(|| "Server address must include a port.".to_owned())?;
    let host = address[..separator].trim();
    let port = parse_port(&address[separator + 1..])?;

    if host.is_empty() {
        return Err("Server host is required.".to_owned());
    }

    Ok((host.to_owned(), port))
}

fn parse_port(port: &str) -> Result<u16, String> {
    port.trim()
        .parse::<u16>()
        .map_err(|_| "Server port is invalid.".to_owned())
}
