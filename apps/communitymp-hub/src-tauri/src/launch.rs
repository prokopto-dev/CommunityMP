use serde::{Deserialize, Serialize};

const GAME_EXECUTABLE: &str = "communitymp";
const DEFAULT_GAME_PORT: u16 = 25565;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LaunchRequest {
    address: String,
    account_name: String,
    server_password: Option<String>,
    mod_manager_mode: Option<String>,
    mod_profile_name: Option<String>,
    data_directories: Option<Vec<String>>,
    content_files: Option<Vec<String>>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LaunchPlan {
    executable: String,
    arguments: Vec<String>,
    display_command: String,
    warnings: Vec<String>,
}

#[tauri::command]
pub fn build_launch_plan(request: LaunchRequest) -> Result<LaunchPlan, String> {
    let address = normalize_server_address(&request.address)?;
    let account_name = request.account_name.trim();

    if account_name.is_empty() {
        return Err("Account username is required before launch.".to_owned());
    }

    let mut arguments = vec![
        "--client".to_owned(),
        format!("--connect={address}"),
        format!("--name={account_name}"),
    ];
    let mut display_arguments = arguments.clone();
    let mut warnings = vec![
        "Account passwords are never included in launch arguments.".to_owned(),
        "Server join passwords are connection-only and are not saved by this launcher.".to_owned(),
    ];

    let data_directories = cleaned_values(request.data_directories.as_deref());
    let content_files = cleaned_values(request.content_files.as_deref());

    if let Some(password) = request
        .server_password
        .as_deref()
        .map(str::trim)
        .filter(|text| !text.is_empty())
    {
        arguments.push(format!("--password={password}"));
        display_arguments.push("--password=<server-password>".to_owned());
    }

    for data in &data_directories {
        arguments.push(format!("--data={data}"));
        display_arguments.push(format!("--data={data}"));
    }

    for content in &content_files {
        arguments.push(format!("--content={content}"));
        display_arguments.push(format!("--content={content}"));
    }

    if request.address.trim() != address {
        warnings.push(format!("Normalized server address to {address}."));
    }

    if let Some(profile) = request
        .mod_profile_name
        .as_deref()
        .map(str::trim)
        .filter(|text| !text.is_empty())
    {
        warnings.push(format!("Using local mod profile \"{profile}\"."));
    }

    if let Some(mode) = request
        .mod_manager_mode
        .as_deref()
        .map(str::trim)
        .filter(|text| !text.is_empty())
    {
        warnings.push(format!("Mod manager compatibility mode: {mode}."));
    }

    if !data_directories.is_empty() || !content_files.is_empty() {
        warnings.push("Mod setup arguments are local launch hints; server-side plugin checks still decide compatibility.".to_owned());
    }

    let display_command = std::iter::once(GAME_EXECUTABLE.to_owned())
        .chain(
            display_arguments
                .iter()
                .map(|argument| quote_argument(argument)),
        )
        .collect::<Vec<_>>()
        .join(" ");

    Ok(LaunchPlan {
        executable: GAME_EXECUTABLE.to_owned(),
        arguments,
        display_command,
        warnings,
    })
}

fn normalize_server_address(address: &str) -> Result<String, String> {
    let address = address.trim();
    if address.is_empty() {
        return Err("Server address is required.".to_owned());
    }

    if address.starts_with('[') && address.contains("]:") {
        return Ok(address.to_owned());
    }

    if address.matches(':').count() > 1 {
        return Ok(format!("[{address}]:{DEFAULT_GAME_PORT}"));
    }

    if address
        .rsplit_once(':')
        .and_then(|(_, port)| port.parse::<u16>().ok())
        .is_some()
    {
        return Ok(address.to_owned());
    }

    Ok(format!("{address}:{DEFAULT_GAME_PORT}"))
}

fn cleaned_values(values: Option<&[String]>) -> Vec<String> {
    values
        .unwrap_or(&[])
        .iter()
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
        .map(ToOwned::to_owned)
        .collect()
}

fn quote_argument(argument: &str) -> String {
    if argument.chars().any(char::is_whitespace) {
        format!("\"{}\"", argument.replace('"', "\\\""))
    } else {
        argument.to_owned()
    }
}
