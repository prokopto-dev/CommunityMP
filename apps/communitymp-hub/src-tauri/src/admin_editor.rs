use quick_xml::events::{BytesStart, Event};
use quick_xml::Reader;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Component, Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const BANLIST_RELATIVE_PATH: &str = "saves/server/security/banlist.xml";
const DATA_FILES_RELATIVE_PATH: &str = "saves/server/config/data-files.xml";
const WORLD_MANIFEST_RELATIVE_PATH: &str = "saves/world/manifest.xml";
const WORLD_CORE_RELATIVE_PATH: &str = "saves/world/state/core.xml";
const WORLD_GLOBAL_RELATIVE_PATH: &str = "saves/world/state/global.xml";
const MAX_DOCUMENTS: usize = 800;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AdminWorkspace {
    pub data_path: String,
    pub server: ServerAdminState,
    pub world: WorldAdminState,
    pub accounts: Vec<AccountSummary>,
    pub documents: Vec<DocumentSummary>,
    pub issues: Vec<String>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ServerAdminState {
    pub ban_list: BanListDocument,
    pub data_files: DataFileDocument,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BanListDocument {
    pub path: String,
    pub exists: bool,
    pub player_names: Vec<String>,
    pub ip_addresses: Vec<String>,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DataFileRequirement {
    pub name: String,
    pub checksums: Vec<String>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DataFileDocument {
    pub path: String,
    pub exists: bool,
    pub requirements: Vec<DataFileRequirement>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WorldAdminState {
    pub manifest: Option<DocumentSummary>,
    pub core: Option<DocumentSummary>,
    pub global: Option<DocumentSummary>,
    pub cells: Vec<WorldEntrySummary>,
    pub record_stores: Vec<WorldEntrySummary>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WorldEntrySummary {
    pub name: String,
    pub path: String,
    pub kind: String,
    pub object_count: Option<usize>,
    pub record_count: Option<usize>,
    pub size_bytes: u64,
    pub modified_at: Option<u64>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AccountSummary {
    pub account: String,
    pub path: String,
    pub character_count: usize,
    pub characters: Vec<CharacterSummary>,
    pub size_bytes: u64,
    pub modified_at: Option<u64>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CharacterSummary {
    pub name: String,
    pub path: String,
    pub level: Option<i64>,
    pub cell: String,
    pub size_bytes: u64,
    pub modified_at: Option<u64>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DocumentSummary {
    pub path: String,
    pub kind: String,
    pub domain: String,
    pub schema_version: Option<String>,
    pub saved_at: Option<String>,
    pub size_bytes: u64,
    pub modified_at: Option<u64>,
    pub editable: bool,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DocumentContent {
    pub path: String,
    pub content: String,
    pub size_bytes: u64,
    pub modified_at: Option<u64>,
    pub structured: Option<SaveDocument>,
    pub parse_error: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WriteResult {
    pub saved_paths: Vec<String>,
    pub backup_paths: Vec<String>,
    pub warnings: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AdminStateSaveRequest {
    pub data_path: String,
    pub ban_list: EditableBanList,
    pub data_files: Vec<DataFileRequirement>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EditableBanList {
    pub player_names: Vec<String>,
    pub ip_addresses: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DocumentSaveRequest {
    pub data_path: String,
    pub relative_path: String,
    pub content: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StructuredDocumentSaveRequest {
    pub data_path: String,
    pub relative_path: String,
    pub document: SaveDocument,
}

#[derive(Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveDocument {
    attributes: HashMap<String, String>,
    data: Option<SaveNode>,
}

#[derive(Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveNode {
    key: String,
    key_type: String,
    value_type: String,
    text: String,
    children: Vec<SaveNode>,
}

#[tauri::command]
pub fn load_admin_workspace(data_path: String) -> Result<AdminWorkspace, String> {
    let data_root = resolve_data_root(&data_path)?;
    let mut issues = Vec::new();
    let ban_list = load_ban_list(&data_root, &mut issues);
    let data_files = load_data_files(&data_root, &mut issues);
    let documents = scan_documents(&data_root, &mut issues)?;
    let world = scan_world(&data_root, &documents, &mut issues)?;
    let accounts = scan_accounts(&data_root, &mut issues)?;

    Ok(AdminWorkspace {
        data_path: data_root.display().to_string(),
        server: ServerAdminState {
            ban_list,
            data_files,
        },
        world,
        accounts,
        documents,
        issues,
    })
}

#[tauri::command]
pub fn read_admin_document(
    data_path: String,
    relative_path: String,
) -> Result<DocumentContent, String> {
    let data_root = resolve_data_root(&data_path)?;
    let relative_path = normalize_relative_path(&relative_path)?;
    let full_path = resolve_relative_file(&data_root, &relative_path)?;
    let content = fs::read_to_string(&full_path)
        .map_err(|error| format!("Could not read {}: {error}", relative_path.display()))?;
    let metadata = fs::metadata(&full_path)
        .map_err(|error| format!("Could not inspect {}: {error}", relative_path.display()))?;
    let (structured, parse_error) = match parse_save_document(&content) {
        Ok(document) => (Some(document), None),
        Err(error) => (None, Some(error)),
    };

    Ok(DocumentContent {
        path: relative_path_to_string(&relative_path),
        content,
        size_bytes: metadata.len(),
        modified_at: modified_seconds(&metadata),
        structured,
        parse_error,
    })
}

#[tauri::command]
pub fn save_admin_document(request: DocumentSaveRequest) -> Result<WriteResult, String> {
    let data_root = resolve_data_root(&request.data_path)?;
    let relative_path = normalize_relative_path(&request.relative_path)?;

    if relative_path.extension().and_then(|value| value.to_str()) != Some("xml") {
        return Err("Only CommunityMP XML documents can be saved by the admin editor.".to_owned());
    }

    parse_save_document(&request.content).map_err(|error| {
        format!(
            "XML validation failed for {}: {error}",
            relative_path.display()
        )
    })?;

    let write = write_relative_file(&data_root, &relative_path, &request.content)?;
    Ok(WriteResult {
        saved_paths: vec![relative_path_to_string(&relative_path)],
        backup_paths: write.backup_path.into_iter().collect(),
        warnings: Vec::new(),
    })
}

#[tauri::command]
pub fn save_admin_structured_document(
    request: StructuredDocumentSaveRequest,
) -> Result<WriteResult, String> {
    let data_root = resolve_data_root(&request.data_path)?;
    let relative_path = normalize_relative_path(&request.relative_path)?;

    if relative_path.extension().and_then(|value| value.to_str()) != Some("xml") {
        return Err("Only CommunityMP XML documents can be saved by the admin editor.".to_owned());
    }

    let mut document = normalize_save_document(request.document)?;
    document
        .attributes
        .insert("savedAt".to_owned(), unix_now().to_string());
    let content = encode_save_document(&document);
    parse_save_document(&content).map_err(|error| {
        format!(
            "Structured XML validation failed for {}: {error}",
            relative_path.display()
        )
    })?;

    let write = write_relative_file(&data_root, &relative_path, &content)?;
    Ok(WriteResult {
        saved_paths: vec![relative_path_to_string(&relative_path)],
        backup_paths: write.backup_path.into_iter().collect(),
        warnings: Vec::new(),
    })
}

#[tauri::command]
pub fn save_admin_state(request: AdminStateSaveRequest) -> Result<WriteResult, String> {
    let data_root = resolve_data_root(&request.data_path)?;
    let ban_list = normalize_ban_list(request.ban_list);
    let data_files = normalize_data_files(request.data_files);
    let mut saved_paths = Vec::new();
    let mut backup_paths = Vec::new();
    let mut warnings = Vec::new();

    if ban_list.player_names.len() + ban_list.ip_addresses.len() == 0 {
        warnings.push(
            "Saved an empty banlist. That is valid, but no players or IPs are blocked.".to_owned(),
        );
    }

    let ban_relative = PathBuf::from(BANLIST_RELATIVE_PATH);
    let ban_content = encode_ban_list_xml(&ban_list);
    let ban_write = write_relative_file(&data_root, &ban_relative, &ban_content)?;
    saved_paths.push(BANLIST_RELATIVE_PATH.to_owned());
    backup_paths.extend(ban_write.backup_path);

    let data_files_relative = PathBuf::from(DATA_FILES_RELATIVE_PATH);
    let data_files_content = encode_data_files_xml(&data_files);
    let data_files_write =
        write_relative_file(&data_root, &data_files_relative, &data_files_content)?;
    saved_paths.push(DATA_FILES_RELATIVE_PATH.to_owned());
    backup_paths.extend(data_files_write.backup_path);

    Ok(WriteResult {
        saved_paths,
        backup_paths,
        warnings,
    })
}

fn resolve_data_root(input: &str) -> Result<PathBuf, String> {
    let raw = input.trim();
    if raw.is_empty() {
        return Err("Enter a server data folder or CommunityMP install folder first.".to_owned());
    }

    let path = PathBuf::from(raw);
    let candidate = if path.join("server").join("data").is_dir() {
        path.join("server").join("data")
    } else if path.join("data").join("saves").is_dir() {
        path.join("data")
    } else {
        path
    };

    if !candidate.is_dir() {
        return Err(format!(
            "Server data folder does not exist: {}",
            candidate.display()
        ));
    }

    candidate
        .canonicalize()
        .map_err(|error| format!("Could not resolve {}: {error}", candidate.display()))
}

fn normalize_relative_path(input: &str) -> Result<PathBuf, String> {
    let replaced = input.replace('\\', "/");
    let path = Path::new(&replaced);

    if path.is_absolute() {
        return Err("Document paths must be relative to server/data.".to_owned());
    }

    let mut output = PathBuf::new();
    for component in path.components() {
        match component {
            Component::Normal(segment) => output.push(segment),
            Component::CurDir => {}
            _ => return Err("Document paths cannot contain parent-directory segments.".to_owned()),
        }
    }

    if output.as_os_str().is_empty() {
        return Err("Document path is empty.".to_owned());
    }

    Ok(output)
}

fn resolve_relative_file(data_root: &Path, relative_path: &Path) -> Result<PathBuf, String> {
    let full_path = data_root.join(relative_path);
    let parent = full_path
        .parent()
        .ok_or_else(|| format!("Invalid document path: {}", relative_path.display()))?;

    if !parent.exists() {
        return Err(format!(
            "Document folder does not exist: {}",
            parent.display()
        ));
    }

    let canonical_parent = parent
        .canonicalize()
        .map_err(|error| format!("Could not resolve {}: {error}", parent.display()))?;
    if !canonical_parent.starts_with(data_root) {
        return Err("Refusing to access a document outside server/data.".to_owned());
    }

    Ok(full_path)
}

struct WriteFileResult {
    backup_path: Option<String>,
}

fn write_relative_file(
    data_root: &Path,
    relative_path: &Path,
    content: &str,
) -> Result<WriteFileResult, String> {
    let full_path = data_root.join(relative_path);
    let parent = full_path
        .parent()
        .ok_or_else(|| format!("Invalid document path: {}", relative_path.display()))?;
    fs::create_dir_all(parent)
        .map_err(|error| format!("Could not create {}: {error}", parent.display()))?;

    let canonical_parent = parent
        .canonicalize()
        .map_err(|error| format!("Could not resolve {}: {error}", parent.display()))?;
    if !canonical_parent.starts_with(data_root) {
        return Err("Refusing to write a document outside server/data.".to_owned());
    }

    let temp_path = full_path.with_extension(format!(
        "{}tmp",
        full_path
            .extension()
            .and_then(|extension| extension.to_str())
            .map(|extension| format!("{extension}."))
            .unwrap_or_default()
    ));
    let backup_path = full_path.with_extension(format!(
        "{}bak",
        full_path
            .extension()
            .and_then(|extension| extension.to_str())
            .map(|extension| format!("{extension}."))
            .unwrap_or_default()
    ));

    fs::write(&temp_path, content)
        .map_err(|error| format!("Could not write temp file {}: {error}", temp_path.display()))?;

    let mut backup_relative = None;
    if full_path.exists() {
        if backup_path.exists() {
            fs::remove_file(&backup_path).map_err(|error| {
                format!(
                    "Could not replace backup {}: {error}",
                    backup_path.display()
                )
            })?;
        }
        fs::rename(&full_path, &backup_path).map_err(|error| {
            format!("Could not create backup {}: {error}", backup_path.display())
        })?;
        backup_relative = backup_path
            .strip_prefix(data_root)
            .ok()
            .map(relative_path_to_string);
    }

    if let Err(error) = fs::rename(&temp_path, &full_path) {
        if backup_path.exists() && !full_path.exists() {
            let _ = fs::rename(&backup_path, &full_path);
        }
        return Err(format!(
            "Could not promote temp save {}: {error}",
            temp_path.display()
        ));
    }

    Ok(WriteFileResult {
        backup_path: backup_relative,
    })
}

fn load_ban_list(data_root: &Path, issues: &mut Vec<String>) -> BanListDocument {
    let path = PathBuf::from(BANLIST_RELATIVE_PATH);
    let full_path = data_root.join(&path);

    if !full_path.exists() {
        issues.push(format!("Missing {}", BANLIST_RELATIVE_PATH));
        return BanListDocument {
            path: BANLIST_RELATIVE_PATH.to_owned(),
            exists: false,
            player_names: Vec::new(),
            ip_addresses: Vec::new(),
        };
    }

    match read_save_document(&full_path) {
        Ok(document) => {
            let data = document.data.as_ref();
            BanListDocument {
                path: BANLIST_RELATIVE_PATH.to_owned(),
                exists: true,
                player_names: read_string_array(data.and_then(|node| node.child("playerNames"))),
                ip_addresses: read_string_array(data.and_then(|node| node.child("ipAddresses"))),
            }
        }
        Err(error) => {
            issues.push(format!(
                "Could not parse {}: {error}",
                BANLIST_RELATIVE_PATH
            ));
            BanListDocument {
                path: BANLIST_RELATIVE_PATH.to_owned(),
                exists: true,
                player_names: Vec::new(),
                ip_addresses: Vec::new(),
            }
        }
    }
}

fn load_data_files(data_root: &Path, issues: &mut Vec<String>) -> DataFileDocument {
    let path = PathBuf::from(DATA_FILES_RELATIVE_PATH);
    let full_path = data_root.join(&path);

    if !full_path.exists() {
        issues.push(format!("Missing {}", DATA_FILES_RELATIVE_PATH));
        return DataFileDocument {
            path: DATA_FILES_RELATIVE_PATH.to_owned(),
            exists: false,
            requirements: Vec::new(),
        };
    }

    match read_save_document(&full_path) {
        Ok(document) => {
            let mut requirements = Vec::new();
            if let Some(data) = document.data.as_ref() {
                for entry in &data.children {
                    for plugin in &entry.children {
                        requirements.push(DataFileRequirement {
                            name: plugin.key.clone(),
                            checksums: read_string_array(Some(plugin)),
                        });
                    }
                }
            }

            DataFileDocument {
                path: DATA_FILES_RELATIVE_PATH.to_owned(),
                exists: true,
                requirements,
            }
        }
        Err(error) => {
            issues.push(format!(
                "Could not parse {}: {error}",
                DATA_FILES_RELATIVE_PATH
            ));
            DataFileDocument {
                path: DATA_FILES_RELATIVE_PATH.to_owned(),
                exists: true,
                requirements: Vec::new(),
            }
        }
    }
}

fn scan_documents(
    data_root: &Path,
    issues: &mut Vec<String>,
) -> Result<Vec<DocumentSummary>, String> {
    let saves_root = data_root.join("saves");
    if !saves_root.exists() {
        issues.push("No saves folder found under server/data.".to_owned());
        return Ok(Vec::new());
    }

    let mut documents = Vec::new();
    collect_xml_documents(data_root, &saves_root, &mut documents, issues)?;
    documents.sort_by(|left, right| left.path.cmp(&right.path));

    if documents.len() > MAX_DOCUMENTS {
        issues.push(format!(
            "Indexed the first {MAX_DOCUMENTS} XML documents. Narrow the folder or use source search for very large shards."
        ));
        documents.truncate(MAX_DOCUMENTS);
    }

    Ok(documents)
}

fn collect_xml_documents(
    data_root: &Path,
    directory: &Path,
    output: &mut Vec<DocumentSummary>,
    issues: &mut Vec<String>,
) -> Result<(), String> {
    for entry in fs::read_dir(directory)
        .map_err(|error| format!("Could not scan {}: {error}", directory.display()))?
    {
        let entry = entry.map_err(|error| format!("Could not read directory entry: {error}"))?;
        let path = entry.path();
        if path.is_dir() {
            collect_xml_documents(data_root, &path, output, issues)?;
        } else if path.extension().and_then(|value| value.to_str()) == Some("xml") {
            match summarize_document(data_root, &path) {
                Ok(summary) => output.push(summary),
                Err(error) => issues.push(error),
            }
        }
    }

    Ok(())
}

fn summarize_document(data_root: &Path, path: &Path) -> Result<DocumentSummary, String> {
    let metadata = fs::metadata(path)
        .map_err(|error| format!("Could not inspect {}: {error}", path.display()))?;
    let relative_path = path
        .strip_prefix(data_root)
        .map_err(|_| format!("Document is outside data root: {}", path.display()))?;
    let content = fs::read_to_string(path)
        .map_err(|error| format!("Could not read {}: {error}", path.display()))?;
    let document = parse_save_document(&content)?;

    Ok(DocumentSummary {
        path: relative_path_to_string(relative_path),
        kind: document
            .attributes
            .get("kind")
            .cloned()
            .unwrap_or_else(|| "xml".to_owned()),
        domain: document
            .attributes
            .get("domain")
            .cloned()
            .unwrap_or_else(|| "unknown".to_owned()),
        schema_version: document.attributes.get("saveSchemaVersion").cloned(),
        saved_at: document.attributes.get("savedAt").cloned(),
        size_bytes: metadata.len(),
        modified_at: modified_seconds(&metadata),
        editable: true,
    })
}

fn scan_world(
    data_root: &Path,
    documents: &[DocumentSummary],
    issues: &mut Vec<String>,
) -> Result<WorldAdminState, String> {
    let manifest = documents
        .iter()
        .find(|document| document.path == WORLD_MANIFEST_RELATIVE_PATH)
        .cloned();
    let core = documents
        .iter()
        .find(|document| document.path == WORLD_CORE_RELATIVE_PATH)
        .cloned();
    let global = documents
        .iter()
        .find(|document| document.path == WORLD_GLOBAL_RELATIVE_PATH)
        .cloned();

    let cells = scan_world_entries(data_root, "saves/world/cells", "cell", issues)?;
    let record_stores =
        scan_world_entries(data_root, "saves/world/recordstores", "recordstore", issues)?;

    Ok(WorldAdminState {
        manifest,
        core,
        global,
        cells,
        record_stores,
    })
}

fn scan_world_entries(
    data_root: &Path,
    relative_root: &str,
    kind: &str,
    issues: &mut Vec<String>,
) -> Result<Vec<WorldEntrySummary>, String> {
    let root = data_root.join(relative_root);
    if !root.exists() {
        return Ok(Vec::new());
    }

    let mut entries = Vec::new();
    for directory_entry in fs::read_dir(&root)
        .map_err(|error| format!("Could not scan {}: {error}", root.display()))?
    {
        let directory_entry =
            directory_entry.map_err(|error| format!("Could not read world entry: {error}"))?;
        if !directory_entry.path().is_dir() {
            continue;
        }

        let file_name = if kind == "cell" {
            "cell.xml"
        } else {
            "records.xml"
        };
        let file_path = directory_entry.path().join(file_name);
        if !file_path.exists() {
            continue;
        }

        match summarize_world_entry(data_root, &file_path, kind) {
            Ok(entry) => entries.push(entry),
            Err(error) => issues.push(error),
        }
    }

    entries.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(entries)
}

fn summarize_world_entry(
    data_root: &Path,
    path: &Path,
    kind: &str,
) -> Result<WorldEntrySummary, String> {
    let metadata = fs::metadata(path)
        .map_err(|error| format!("Could not inspect {}: {error}", path.display()))?;
    let document = read_save_document(path)?;
    let relative_path = path
        .strip_prefix(data_root)
        .map_err(|_| format!("World entry is outside data root: {}", path.display()))?;
    let name = if kind == "cell" {
        document.attributes.get("cell").cloned().unwrap_or_else(|| {
            path.parent()
                .and_then(Path::file_name)
                .and_then(|v| v.to_str())
                .unwrap_or("cell")
                .to_owned()
        })
    } else {
        document
            .attributes
            .get("storeType")
            .cloned()
            .unwrap_or_else(|| {
                path.parent()
                    .and_then(Path::file_name)
                    .and_then(|v| v.to_str())
                    .unwrap_or("recordstore")
                    .to_owned()
            })
    };
    let data = document.data.as_ref();

    Ok(WorldEntrySummary {
        name,
        path: relative_path_to_string(relative_path),
        kind: kind.to_owned(),
        object_count: data
            .and_then(|node| node.child("objectData"))
            .map(|node| node.children.len()),
        record_count: data
            .and_then(|node| node.child("generatedRecords"))
            .map(|node| node.children.len()),
        size_bytes: metadata.len(),
        modified_at: modified_seconds(&metadata),
    })
}

fn scan_accounts(
    data_root: &Path,
    issues: &mut Vec<String>,
) -> Result<Vec<AccountSummary>, String> {
    let saves_root = data_root.join("saves");
    if !saves_root.exists() {
        return Ok(Vec::new());
    }

    let mut accounts = Vec::new();
    for entry in fs::read_dir(&saves_root).map_err(|error| {
        format!(
            "Could not scan accounts in {}: {error}",
            saves_root.display()
        )
    })? {
        let entry = entry.map_err(|error| format!("Could not read account entry: {error}"))?;
        if !entry.path().is_dir() {
            continue;
        }

        let folder_name = entry.file_name().to_string_lossy().to_string();
        if folder_name == "server" || folder_name == "world" {
            continue;
        }

        let account_file = entry.path().join("account.xml");
        if !account_file.exists() {
            continue;
        }

        match summarize_account(data_root, &account_file, &folder_name) {
            Ok(account) => accounts.push(account),
            Err(error) => issues.push(error),
        }
    }

    accounts.sort_by(|left, right| left.account.cmp(&right.account));
    Ok(accounts)
}

fn summarize_account(
    data_root: &Path,
    path: &Path,
    fallback_name: &str,
) -> Result<AccountSummary, String> {
    let metadata = fs::metadata(path)
        .map_err(|error| format!("Could not inspect {}: {error}", path.display()))?;
    let document = read_save_document(path)?;
    let data = document.data.as_ref();
    let account = data
        .and_then(|node| node.child("login"))
        .and_then(|node| node.child("name"))
        .map(|node| node.text.clone())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| fallback_name.to_owned());
    let mut characters = Vec::new();
    let character_root = path
        .parent()
        .map(|parent| parent.join("characters"))
        .unwrap_or_default();

    if character_root.exists() {
        collect_character_summaries(data_root, &character_root, &mut characters)?;
    }

    characters.sort_by(|left, right| left.name.cmp(&right.name));
    let relative_path = path
        .strip_prefix(data_root)
        .map_err(|_| format!("Account is outside data root: {}", path.display()))?;

    Ok(AccountSummary {
        account,
        path: relative_path_to_string(relative_path),
        character_count: characters.len(),
        characters,
        size_bytes: metadata.len(),
        modified_at: modified_seconds(&metadata),
    })
}

fn collect_character_summaries(
    data_root: &Path,
    directory: &Path,
    output: &mut Vec<CharacterSummary>,
) -> Result<(), String> {
    for entry in fs::read_dir(directory).map_err(|error| {
        format!(
            "Could not scan characters in {}: {error}",
            directory.display()
        )
    })? {
        let entry = entry.map_err(|error| format!("Could not read character entry: {error}"))?;
        let path = entry.path();
        if path.is_dir() {
            collect_character_summaries(data_root, &path, output)?;
        } else if path.extension().and_then(|value| value.to_str()) == Some("xml") {
            output.push(summarize_character(data_root, &path)?);
        }
    }

    Ok(())
}

fn summarize_character(data_root: &Path, path: &Path) -> Result<CharacterSummary, String> {
    let metadata = fs::metadata(path)
        .map_err(|error| format!("Could not inspect {}: {error}", path.display()))?;
    let document = read_save_document(path)?;
    let data = document.data.as_ref();
    let name = data
        .and_then(|node| node.child("character"))
        .and_then(|node| node.child("name"))
        .map(|node| node.text.clone())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| {
            path.file_stem()
                .and_then(|value| value.to_str())
                .unwrap_or("character")
                .to_owned()
        });
    let level = data
        .and_then(|node| node.child("stats"))
        .and_then(|node| node.child("level"))
        .and_then(|node| node.text.parse::<i64>().ok());
    let cell = data
        .and_then(|node| node.child("location"))
        .and_then(|node| node.child("cell").or_else(|| node.child("cellDescription")))
        .map(|node| node.text.clone())
        .unwrap_or_default();
    let relative_path = path
        .strip_prefix(data_root)
        .map_err(|_| format!("Character is outside data root: {}", path.display()))?;

    Ok(CharacterSummary {
        name,
        path: relative_path_to_string(relative_path),
        level,
        cell,
        size_bytes: metadata.len(),
        modified_at: modified_seconds(&metadata),
    })
}

fn read_save_document(path: &Path) -> Result<SaveDocument, String> {
    let content = fs::read_to_string(path)
        .map_err(|error| format!("Could not read {}: {error}", path.display()))?;
    parse_save_document(&content)
        .map_err(|error| format!("Could not parse {}: {error}", path.display()))
}

fn parse_save_document(content: &str) -> Result<SaveDocument, String> {
    let mut reader = Reader::from_str(content);
    reader.config_mut().trim_text(true);

    let mut save_attributes = None;
    let mut stack: Vec<SaveNode> = Vec::new();
    let mut data = None;

    loop {
        match reader.read_event() {
            Ok(Event::Start(event)) if event.name().as_ref() == b"save" => {
                save_attributes = Some(read_attributes(&event, &reader)?);
            }
            Ok(Event::Start(event)) if event.name().as_ref() == b"node" => {
                stack.push(node_from_event(&event, &reader)?);
            }
            Ok(Event::Empty(event)) if event.name().as_ref() == b"node" => {
                let node = node_from_event(&event, &reader)?;
                append_node(&mut stack, &mut data, node);
            }
            Ok(Event::Text(event)) => {
                if let Some(node) = stack.last_mut() {
                    let text = event
                        .decode()
                        .map_err(|error| format!("Could not decode XML text: {error}"))?;
                    node.text.push_str(&text);
                }
            }
            Ok(Event::CData(event)) => {
                if let Some(node) = stack.last_mut() {
                    let text = event
                        .decode()
                        .map_err(|error| format!("Could not decode XML CDATA: {error}"))?;
                    node.text.push_str(&text);
                }
            }
            Ok(Event::End(event)) if event.name().as_ref() == b"node" => {
                let node = stack
                    .pop()
                    .ok_or_else(|| "Unexpected closing node tag.".to_owned())?;
                append_node(&mut stack, &mut data, node);
            }
            Ok(Event::Eof) => break,
            Ok(_) => {}
            Err(error) => return Err(format!("XML parser error: {error}")),
        }
    }

    if !stack.is_empty() {
        return Err("Unclosed XML node in save document.".to_owned());
    }

    let attributes = save_attributes.ok_or_else(|| "Missing <save> root element.".to_owned())?;
    Ok(SaveDocument { attributes, data })
}

fn append_node(stack: &mut [SaveNode], data: &mut Option<SaveNode>, node: SaveNode) {
    if let Some(parent) = stack.last_mut() {
        parent.children.push(node);
    } else if node.key == "data" {
        *data = Some(node);
    }
}

fn node_from_event(event: &BytesStart<'_>, reader: &Reader<&[u8]>) -> Result<SaveNode, String> {
    let attributes = read_attributes(event, reader)?;
    Ok(SaveNode {
        key: decode_key(&attributes),
        key_type: attributes.get("keyType").cloned().unwrap_or_else(|| {
            infer_key_type(
                attributes
                    .get("key")
                    .map(String::as_str)
                    .unwrap_or_default(),
            )
        }),
        value_type: attributes
            .get("type")
            .cloned()
            .unwrap_or_else(|| "string".to_owned()),
        text: String::new(),
        children: Vec::new(),
    })
}

fn read_attributes(
    event: &BytesStart<'_>,
    reader: &Reader<&[u8]>,
) -> Result<HashMap<String, String>, String> {
    let mut attributes = HashMap::new();

    for attribute in event.attributes().with_checks(false) {
        let attribute =
            attribute.map_err(|error| format!("Could not read XML attribute: {error}"))?;
        let key = std::str::from_utf8(attribute.key.as_ref())
            .map_err(|error| format!("Invalid XML attribute name: {error}"))?
            .to_owned();
        let value = attribute
            .decode_and_unescape_value(reader.decoder())
            .map_err(|error| format!("Could not decode XML attribute {key}: {error}"))?
            .into_owned();
        attributes.insert(key, value);
    }

    Ok(attributes)
}

fn decode_key(attributes: &HashMap<String, String>) -> String {
    attributes.get("key").cloned().unwrap_or_default()
}

fn infer_key_type(key: &str) -> String {
    if !key.is_empty() && key.chars().all(|character| character.is_ascii_digit()) {
        "number".to_owned()
    } else {
        "string".to_owned()
    }
}

impl SaveNode {
    fn child(&self, key: &str) -> Option<&SaveNode> {
        self.children.iter().find(|child| child.key == key)
    }
}

fn read_string_array(node: Option<&SaveNode>) -> Vec<String> {
    let mut output = Vec::new();
    let mut seen = HashSet::new();

    if let Some(node) = node {
        for child in &node.children {
            if child.value_type == "string" || child.children.is_empty() {
                let value = child.text.trim().to_owned();
                if !value.is_empty() && seen.insert(value.to_lowercase()) {
                    output.push(value);
                }
            }
        }
    }

    output
}

fn normalize_ban_list(input: EditableBanList) -> EditableBanList {
    EditableBanList {
        player_names: normalize_strings(input.player_names),
        ip_addresses: normalize_strings(input.ip_addresses),
    }
}

fn normalize_data_files(input: Vec<DataFileRequirement>) -> Vec<DataFileRequirement> {
    let mut output = Vec::new();
    let mut seen = HashSet::new();

    for requirement in input {
        let name = requirement.name.trim().to_owned();
        if name.is_empty() || !seen.insert(name.to_lowercase()) {
            continue;
        }

        output.push(DataFileRequirement {
            name,
            checksums: normalize_strings(requirement.checksums),
        });
    }

    output
}

fn normalize_strings(input: Vec<String>) -> Vec<String> {
    let mut output = Vec::new();
    let mut seen = HashSet::new();

    for value in input {
        let trimmed = value.trim();
        if !trimmed.is_empty() && seen.insert(trimmed.to_lowercase()) {
            output.push(trimmed.to_owned());
        }
    }

    output
}

fn normalize_save_document(mut document: SaveDocument) -> Result<SaveDocument, String> {
    document
        .attributes
        .entry("format".to_owned())
        .or_insert_with(|| "CommunityMP XML Save".to_owned());
    document
        .attributes
        .entry("schemaVersion".to_owned())
        .or_insert_with(|| "1".to_owned());

    let data = document
        .data
        .take()
        .ok_or_else(|| "Structured save documents need a data root.".to_owned())?;
    document.data = Some(normalize_save_node(data, true)?);
    Ok(document)
}

fn normalize_save_node(mut node: SaveNode, is_root: bool) -> Result<SaveNode, String> {
    node.key = node.key.trim().to_owned();
    node.key_type = normalize_key_type(&node.key_type, &node.key);
    node.value_type = normalize_value_type(&node.value_type);
    node.text = node.text.trim().to_owned();

    if node.key.is_empty() {
        return Err("Structured XML fields cannot have an empty key.".to_owned());
    }

    if is_root {
        node.key = "data".to_owned();
        node.key_type = "string".to_owned();
        node.value_type = "table".to_owned();
    }

    let mut normalized_children = Vec::new();
    for child in node.children {
        normalized_children.push(normalize_save_node(child, false)?);
    }

    if !normalized_children.is_empty() {
        node.value_type = "table".to_owned();
        node.text.clear();
    }

    if node.value_type == "table" {
        node.text.clear();
    } else {
        normalized_children.clear();
    }

    node.children = normalized_children;
    Ok(node)
}

fn normalize_key_type(input: &str, key: &str) -> String {
    let normalized = input.trim().to_ascii_lowercase();
    if normalized == "number" || normalized == "string" {
        normalized
    } else {
        infer_key_type(key)
    }
}

fn normalize_value_type(input: &str) -> String {
    match input.trim().to_ascii_lowercase().as_str() {
        "boolean" => "boolean".to_owned(),
        "number" => "number".to_owned(),
        "table" => "table".to_owned(),
        _ => "string".to_owned(),
    }
}

fn encode_save_document(document: &SaveDocument) -> String {
    let mut output = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<save".to_owned();
    let mut attributes: Vec<_> = document.attributes.iter().collect();
    attributes.sort_by(|left, right| left.0.cmp(right.0));

    for (key, value) in attributes {
        output.push_str(&format!(" {}=\"{}\"", key, escape_xml(value)));
    }

    output.push_str(">\n");
    if let Some(data) = &document.data {
        encode_save_node(&mut output, data, 1);
    }
    output.push_str("</save>\n");
    output
}

fn encode_save_node(output: &mut String, node: &SaveNode, indentation: usize) {
    let indent = "    ".repeat(indentation);
    output.push_str(&format!(
        "{indent}<node key=\"{}\" keyType=\"{}\" type=\"{}\"",
        escape_xml(&node.key),
        escape_xml(&node.key_type),
        escape_xml(&node.value_type)
    ));

    if node.children.is_empty() {
        output.push('>');
        output.push_str(&escape_xml(&node.text));
        output.push_str("</node>\n");
        return;
    }

    output.push_str(">\n");
    for child in &node.children {
        encode_save_node(output, child, indentation + 1);
    }
    output.push_str(&format!("{indent}</node>\n"));
}

fn encode_ban_list_xml(ban_list: &EditableBanList) -> String {
    let mut output = save_header("server-banlist", "server-security", 1);
    output.push_str("    <node key=\"data\" keyType=\"string\" type=\"table\">\n");
    encode_string_array(&mut output, "ipAddresses", &ban_list.ip_addresses, 2);
    encode_string_array(&mut output, "playerNames", &ban_list.player_names, 2);
    output.push_str("    </node>\n</save>\n");
    output
}

fn encode_data_files_xml(requirements: &[DataFileRequirement]) -> String {
    let mut output = save_header("server-data-files", "server-config", 1);
    output.push_str("    <node key=\"data\" keyType=\"string\" type=\"table\">\n");

    for (index, requirement) in requirements.iter().enumerate() {
        let entry_index = index + 1;
        output.push_str(&format!(
            "        <node key=\"{entry_index}\" keyType=\"number\" type=\"table\">\n"
        ));
        output.push_str(&format!(
            "            <node key=\"{}\" keyType=\"string\" type=\"table\">\n",
            escape_xml(&requirement.name)
        ));

        for (checksum_index, checksum) in requirement.checksums.iter().enumerate() {
            output.push_str(&format!(
                "                <node key=\"{}\" keyType=\"number\" type=\"string\">{}</node>\n",
                checksum_index + 1,
                escape_xml(checksum)
            ));
        }

        output.push_str("            </node>\n");
        output.push_str("        </node>\n");
    }

    output.push_str("    </node>\n</save>\n");
    output
}

fn save_header(kind: &str, domain: &str, schema_version: u8) -> String {
    format!(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<save domain=\"{}\" format=\"CommunityMP XML Save\" kind=\"{}\" saveSchemaVersion=\"{}\" savedAt=\"{}\" schemaVersion=\"1\">\n",
        escape_xml(domain),
        escape_xml(kind),
        schema_version,
        unix_now()
    )
}

fn encode_string_array(output: &mut String, key: &str, values: &[String], indentation: usize) {
    let indent = "    ".repeat(indentation);
    let child_indent = "    ".repeat(indentation + 1);

    output.push_str(&format!(
        "{indent}<node key=\"{}\" keyType=\"string\" type=\"table\">\n",
        escape_xml(key)
    ));
    for (index, value) in values.iter().enumerate() {
        output.push_str(&format!(
            "{child_indent}<node key=\"{}\" keyType=\"number\" type=\"string\">{}</node>\n",
            index + 1,
            escape_xml(value)
        ));
    }
    output.push_str(&format!("{indent}</node>\n"));
}

fn escape_xml(input: &str) -> String {
    input
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

fn unix_now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .unwrap_or(0)
}

fn modified_seconds(metadata: &fs::Metadata) -> Option<u64> {
    metadata
        .modified()
        .ok()
        .and_then(|modified| modified.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_secs())
}

fn relative_path_to_string(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}

#[cfg(test)]
mod tests {
    use super::*;

    struct TempAdminData {
        root: PathBuf,
    }

    impl TempAdminData {
        fn new() -> Self {
            let unique_tick = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or(0);
            let root = std::env::temp_dir().join(format!(
                "communitymp-admin-editor-test-{}-{}",
                std::process::id(),
                unique_tick
            ));
            fs::create_dir_all(&root).expect("create temp root");
            Self { root }
        }

        fn data_path(&self) -> String {
            self.root.display().to_string()
        }
    }

    impl Drop for TempAdminData {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.root);
        }
    }

    #[test]
    fn admin_state_round_trips_structured_xml_and_backups() {
        let temp = TempAdminData::new();
        let request = AdminStateSaveRequest {
            data_path: temp.data_path(),
            ban_list: EditableBanList {
                player_names: vec!["Alex".to_owned(), " alex ".to_owned(), "Morgan".to_owned()],
                ip_addresses: vec!["127.0.0.1".to_owned()],
            },
            data_files: vec![
                DataFileRequirement {
                    name: "Morrowind.esm".to_owned(),
                    checksums: vec!["0x7B6AF5B9".to_owned(), "0x7B6AF5B9".to_owned()],
                },
                DataFileRequirement {
                    name: " ".to_owned(),
                    checksums: vec!["ignored".to_owned()],
                },
            ],
        };

        let first_save = save_admin_state(request).expect("initial save");
        assert_eq!(first_save.saved_paths.len(), 2);
        assert!(first_save.backup_paths.is_empty());

        let workspace = load_admin_workspace(temp.data_path()).expect("load workspace");
        assert_eq!(
            workspace.server.ban_list.player_names,
            vec!["Alex", "Morgan"]
        );
        assert_eq!(workspace.server.ban_list.ip_addresses, vec!["127.0.0.1"]);
        assert_eq!(workspace.server.data_files.requirements.len(), 1);
        assert_eq!(
            workspace.server.data_files.requirements[0].checksums,
            vec!["0x7B6AF5B9"]
        );

        let ban_content = read_admin_document(temp.data_path(), BANLIST_RELATIVE_PATH.to_owned())
            .expect("read banlist");
        let second_save = save_admin_document(DocumentSaveRequest {
            data_path: temp.data_path(),
            relative_path: BANLIST_RELATIVE_PATH.to_owned(),
            content: ban_content.content,
        })
        .expect("source save");
        assert_eq!(second_save.saved_paths, vec![BANLIST_RELATIVE_PATH]);
        assert_eq!(
            second_save.backup_paths,
            vec!["saves/server/security/banlist.xml.bak"]
        );
        assert!(temp
            .root
            .join("saves/server/security/banlist.xml.bak")
            .exists());
    }

    #[test]
    fn structured_document_save_rebuilds_editable_tree_and_backup() {
        let temp = TempAdminData::new();
        let relative_path = "saves/world/state/core.xml";
        let full_path = temp.root.join(relative_path);
        fs::create_dir_all(full_path.parent().expect("core parent")).expect("create core parent");
        fs::write(
            &full_path,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<save domain=\"world\" format=\"CommunityMP XML Save\" kind=\"world-core\" saveSchemaVersion=\"2\" savedAt=\"1\" schemaVersion=\"1\">\n    <node key=\"data\" keyType=\"string\" type=\"table\">\n        <node key=\"time\" keyType=\"string\" type=\"number\">10</node>\n    </node>\n</save>\n",
        )
        .expect("write core");

        let content = read_admin_document(temp.data_path(), relative_path.to_owned())
            .expect("read structured document");
        let mut document = content.structured.expect("structured tree");
        let data = document.data.as_mut().expect("data root");
        data.children.push(SaveNode {
            key: "weather".to_owned(),
            key_type: "string".to_owned(),
            value_type: "string".to_owned(),
            text: "clear".to_owned(),
            children: Vec::new(),
        });

        let result = save_admin_structured_document(StructuredDocumentSaveRequest {
            data_path: temp.data_path(),
            relative_path: relative_path.to_owned(),
            document,
        })
        .expect("save structured document");

        assert_eq!(result.saved_paths, vec![relative_path]);
        assert_eq!(result.backup_paths, vec!["saves/world/state/core.xml.bak"]);

        let saved = read_save_document(&full_path).expect("parse saved document");
        let data = saved.data.as_ref().expect("saved data root");
        assert_eq!(data.child("time").expect("time").text, "10");
        assert_eq!(data.child("weather").expect("weather").text, "clear");
        assert!(full_path.with_extension("xml.bak").exists());
    }
}
