#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

use tauri::{AppHandle, Emitter, Manager, State, WindowEvent};
use tauri_plugin_dialog::{DialogExt, MessageDialogButtons, MessageDialogKind, MessageDialogResult};

const APP_NAME: &str = "Gastinel Write";

#[derive(Default)]
struct DocState {
    dirty: AtomicBool,
}

#[derive(serde::Serialize)]
struct OpenedFile {
    path: String,
    contents: String,
}

fn read_text(path: &Path) -> Result<String, String> {
    let bytes = fs::read(path).map_err(|e| format!("Lecture impossible : {e}"))?;
    let bytes = bytes.strip_prefix(b"\xEF\xBB\xBF").unwrap_or(&bytes);
    match std::str::from_utf8(bytes) {
        Ok(s) => Ok(s.to_owned()),
        // Fichiers anciens : on retombe sur Windows-1252/Latin-1, octet par octet.
        Err(_) => Ok(bytes.iter().map(|&b| b as char).collect()),
    }
}

/// Écriture atomique : fichier temporaire à côté de la cible, puis renommage,
/// pour ne jamais laisser un document à moitié écrit en cas de coupure.
fn write_text_atomic(path: &Path, contents: &str) -> Result<(), String> {
    let dir = path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    let name = path.file_name().ok_or("Chemin invalide")?.to_string_lossy();
    let tmp = dir.join(format!(".{name}.gw-tmp"));
    let result = (|| {
        let mut f = fs::File::create(&tmp)?;
        f.write_all(contents.as_bytes())?;
        f.sync_all()?;
        fs::rename(&tmp, path)
    })();
    if result.is_err() {
        let _ = fs::remove_file(&tmp);
    }
    result.map_err(|e| format!("Enregistrement impossible : {e}"))
}

fn to_path(p: tauri_plugin_dialog::FilePath) -> Result<PathBuf, String> {
    p.into_path().map_err(|e| e.to_string())
}

#[tauri::command]
async fn open_file(app: AppHandle) -> Result<Option<OpenedFile>, String> {
    let Some(picked) = app
        .dialog()
        .file()
        .add_filter("Texte", &["txt"])
        .add_filter("Tous les fichiers", &["*"])
        .blocking_pick_file()
    else {
        return Ok(None);
    };
    let path = to_path(picked)?;
    let contents = read_text(&path)?;
    Ok(Some(OpenedFile { path: path.to_string_lossy().into_owned(), contents }))
}

#[tauri::command]
async fn open_path(path: String) -> Result<OpenedFile, String> {
    let contents = read_text(Path::new(&path))?;
    Ok(OpenedFile { path, contents })
}

/// `path` absent => boîte « Enregistrer sous ». Renvoie le chemin final, ou None si annulé.
#[tauri::command]
async fn save_file(app: AppHandle, path: Option<String>, contents: String) -> Result<Option<String>, String> {
    let target = match path {
        Some(p) => PathBuf::from(p),
        None => {
            let Some(picked) = app
                .dialog()
                .file()
                .add_filter("Texte", &["txt"])
                .set_file_name("Sans titre.txt")
                .blocking_save_file()
            else {
                return Ok(None);
            };
            to_path(picked)?
        }
    };
    write_text_atomic(&target, &contents)?;
    Ok(Some(target.to_string_lossy().into_owned()))
}

/// Renvoie "save", "discard" ou "cancel".
#[tauri::command]
async fn ask_unsaved(app: AppHandle) -> &'static str {
    let result = app
        .dialog()
        .message("Le document contient des modifications non enregistrées.")
        .title(APP_NAME)
        .kind(MessageDialogKind::Warning)
        .buttons(MessageDialogButtons::YesNoCancelCustom(
            "Enregistrer".into(),
            "Ne pas enregistrer".into(),
            "Annuler".into(),
        ))
        .blocking_show_with_result();
    match result {
        MessageDialogResult::Yes => "save",
        MessageDialogResult::No => "discard",
        MessageDialogResult::Custom(s) if s == "Enregistrer" => "save",
        MessageDialogResult::Custom(s) if s == "Ne pas enregistrer" => "discard",
        _ => "cancel",
    }
}

#[tauri::command]
fn set_dirty(state: State<DocState>, dirty: bool) {
    state.dirty.store(dirty, Ordering::Relaxed);
}

#[tauri::command]
fn quit(app: AppHandle) {
    app.exit(0);
}

#[tauri::command]
fn startup_path() -> Option<String> {
    std::env::args().nth(1).filter(|a| !a.starts_with('-'))
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(DocState::default())
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                let dirty = window.state::<DocState>().dirty.load(Ordering::Relaxed);
                if dirty {
                    api.prevent_close();
                    let _ = window.emit("gw://close-requested", ());
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            open_file, open_path, save_file, ask_unsaved, set_dirty, quit, startup_path
        ])
        .run(tauri::generate_context!())
        .expect("erreur au lancement de Gastinel Write");
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp_dir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gw-test-{name}-{}", std::process::id()));
        fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn strips_utf8_bom() {
        let p = tmp_dir("bom").join("a.txt");
        fs::write(&p, b"\xEF\xBB\xBFbonjour").unwrap();
        assert_eq!(read_text(&p).unwrap(), "bonjour");
    }

    #[test]
    fn falls_back_to_latin1() {
        let p = tmp_dir("latin1").join("a.txt");
        fs::write(&p, b"\xE9t\xE9").unwrap();
        assert_eq!(read_text(&p).unwrap(), "été");
    }

    #[test]
    fn atomic_write_replaces_and_leaves_no_temp() {
        let d = tmp_dir("atomic");
        let p = d.join("doc.txt");
        fs::write(&p, "ancien").unwrap();
        write_text_atomic(&p, "nouveau texte").unwrap();
        assert_eq!(fs::read_to_string(&p).unwrap(), "nouveau texte");
        assert_eq!(fs::read_dir(&d).unwrap().count(), 1);
    }
}
