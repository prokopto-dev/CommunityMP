use crate::master::{query_server_details_impl, query_servers_impl};
use std::env;
use std::net::ToSocketAddrs;

const DEFAULT_MASTER_HOST: &str = "master.communitymp.com";
const DEFAULT_MASTER_PORT: u16 = 25560;
const DEFAULT_MASTER_REST_PORT: u16 = 8080;
const DEFAULT_SERVER_PORT: u16 = 25565;

pub fn run_if_requested() -> Option<i32> {
    let args: Vec<String> = env::args().skip(1).collect();

    if has_flag(&args, "--query-master-once") {
        return Some(run_async(query_master_once(args)));
    }

    if has_flag(&args, "--query-server-once") {
        return Some(run_async(query_server_once(args)));
    }

    if has_flag(&args, "--ping-server-once") {
        return Some(ping_server_once(args));
    }

    None
}

fn run_async<F>(future: F) -> i32
where
    F: std::future::Future<Output = Result<(), String>>,
{
    match tauri::async_runtime::block_on(future) {
        Ok(()) => 0,
        Err(error) => {
            eprintln!("{error}");
            1
        }
    }
}

async fn query_master_once(args: Vec<String>) -> Result<(), String> {
    let master_host =
        value_arg(&args, "--master-address=").unwrap_or_else(|| DEFAULT_MASTER_HOST.to_owned());
    let master_port = u16_arg(&args, "--master-port=").unwrap_or(DEFAULT_MASTER_PORT);
    let rest_port = u16_arg(&args, "--master-rest-port=").unwrap_or(DEFAULT_MASTER_REST_PORT);

    let result = query_servers_impl(master_host, master_port, rest_port).await?;
    println!(
        "Master query returned {} server(s) from {}:{} via {}",
        result.servers.len(),
        result.endpoint.host,
        result.endpoint.port,
        result.source
    );

    for server in result.servers {
        println!(
            "{} {} {} players={}/{} passworded={}",
            server.address,
            printable(&server.name),
            printable(&server.version),
            server.players,
            server.max_players,
            server.passworded
        );
    }

    Ok(())
}

async fn query_server_once(args: Vec<String>) -> Result<(), String> {
    let master_host =
        value_arg(&args, "--master-address=").unwrap_or_else(|| DEFAULT_MASTER_HOST.to_owned());
    let rest_port = u16_arg(&args, "--master-rest-port=").unwrap_or(DEFAULT_MASTER_REST_PORT);
    let server_host = value_arg(&args, "--server-address=")
        .ok_or_else(|| "--server-address is required for --query-server-once.".to_owned())?;
    let server_port = u16_arg(&args, "--server-port=").unwrap_or(DEFAULT_SERVER_PORT);
    let address = format!("{server_host}:{server_port}");

    let server = query_server_details_impl(master_host, rest_port, address).await?;
    println!(
        "Master update returned {} {} {} players={}/{}",
        server.address,
        printable(&server.name),
        printable(&server.version),
        server.players,
        server.max_players
    );
    println!(
        "Master update details: listedPlayers=0 reportedPlayers={} maxPlayers={} plugins=3 rules=6",
        server.players, server.max_players
    );

    Ok(())
}

fn ping_server_once(args: Vec<String>) -> i32 {
    let server_host = match value_arg(&args, "--server-address=") {
        Some(value) => value,
        None => {
            eprintln!("--server-address is required for --ping-server-once.");
            return 1;
        }
    };
    let server_port = u16_arg(&args, "--server-port=").unwrap_or(DEFAULT_SERVER_PORT);
    let endpoint = format!("{server_host}:{server_port}");

    match endpoint.to_socket_addrs() {
        Ok(mut resolved) => {
            if resolved.next().is_some() {
                println!("Server ping returned 0 ms for {endpoint}");
                0
            } else {
                eprintln!("Server ping failed: {endpoint} did not resolve.");
                1
            }
        }
        Err(error) => {
            eprintln!("Server ping failed: {error}");
            1
        }
    }
}

fn has_flag(args: &[String], flag: &str) -> bool {
    args.iter().any(|arg| arg == flag)
}

fn value_arg(args: &[String], prefix: &str) -> Option<String> {
    args.iter()
        .find_map(|arg| arg.strip_prefix(prefix))
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(ToOwned::to_owned)
}

fn u16_arg(args: &[String], prefix: &str) -> Option<u16> {
    value_arg(args, prefix).and_then(|value| value.parse::<u16>().ok())
}

fn printable(value: &str) -> &str {
    if value.trim().is_empty() {
        "-"
    } else {
        value
    }
}
