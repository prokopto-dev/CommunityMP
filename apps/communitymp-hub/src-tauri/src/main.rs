#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod admin_editor;
mod cli;
mod launch;
mod master;

fn main() {
    if let Some(exit_code) = cli::run_if_requested() {
        std::process::exit(exit_code);
    }

    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            admin_editor::load_admin_workspace,
            admin_editor::read_admin_document,
            admin_editor::save_admin_document,
            admin_editor::save_admin_structured_document,
            admin_editor::save_admin_state,
            launch::build_launch_plan,
            master::default_master_endpoint,
            master::query_server_details,
            master::query_servers
        ])
        .run(tauri::generate_context!())
        .expect("failed to run CommunityMP Hub");
}
