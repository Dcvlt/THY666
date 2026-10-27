//! Small native host for the existing Companion and updater implementations.
//! Construction has no USB side effects. Discovery, connection and installation
//! remain explicit, scoped commands owned by the bundled main window.
mod companion;
mod exports;
#[cfg(any(target_os = "windows", test))]
mod windowing;
use serde::Serialize;
use serde_json::{json, Value};
use std::{
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc, Mutex,
    },
};
use tauri::{Emitter, Manager};
use thy666_updater::{package, service::Service};

pub const PROTOCOL: &str = "THY666-DESKTOP/1";

#[derive(Debug, Serialize)]
pub struct DesktopError {
    kind: &'static str,
    message: String,
}
impl DesktopError {
    fn rejected(message: impl Into<String>) -> Self {
        Self {
            kind: "rejected",
            message: message.into(),
        }
    }
    fn unknown(message: impl Into<String>) -> Self {
        Self {
            kind: "unknown",
            message: message.into(),
        }
    }
}
impl From<String> for DesktopError {
    fn from(message: String) -> Self {
        Self::rejected(message)
    }
}
type Reply<T = Value> = Result<T, DesktopError>;

pub struct DesktopState {
    updater: Service,
    companion: Mutex<companion::Companion>,
    simulation: bool,
    pending_installs: AtomicUsize,
    diagnostics: AtomicBool,
}
impl DesktopState {
    pub fn new(simulation: bool, catalog: Option<PathBuf>) -> Result<Self, String> {
        let updater = match catalog {
            Some(path) => Service::new(simulation).with_catalog(path)?,
            None => Service::new(simulation),
        };
        Ok(Self {
            updater,
            companion: Mutex::new(companion::Companion::default()),
            simulation,
            pending_installs: AtomicUsize::new(0),
            diagnostics: AtomicBool::new(false),
        })
    }
    fn info(&self) -> Value {
        json!({"protocol":PROTOCOL,"version":env!("CARGO_PKG_VERSION"),"simulation":self.simulation,
        "platform":std::env::consts::OS,"capabilities":["native-cdc","batched-console","native-updater"]})
    }
    fn updating(&self) -> bool {
        if self.pending_installs.load(Ordering::Acquire) > 0 {
            return true;
        }
        self.updater.status()["operation"]
            .as_object()
            .is_some_and(|op| {
                !matches!(
                    op.get("state").and_then(Value::as_str),
                    Some("failed" | "rejected" | "awaiting-verification")
                )
            })
    }
    fn application_available(&self) -> Reply<()> {
        if self.updating() {
            Err(DesktopError::rejected(
                "The updater owns USB; wait for its terminal status before connecting Client",
            ))
        } else {
            Ok(())
        }
    }
    fn list(&self) -> Reply {
        let mut guard = self
            .companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?;
        self.application_available()?;
        Ok(guard.list(self.simulation)?)
    }
    fn open(&self, id: &str) -> Reply {
        let mut guard = self
            .companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?;
        self.application_available()?;
        Ok(guard.open(id, self.simulation)?)
    }
    fn session(&self, id: &str) -> Reply<Arc<companion::Session>> {
        self.application_available()?;
        Ok(self
            .companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?
            .session(id)?)
    }
    fn close(&self, id: &str) -> Reply<()> {
        self.companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?
            .close(id)?;
        Ok(())
    }
    fn install(&self, request: &Value) -> Reply {
        let guard = self
            .companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?;
        if guard.is_open() {
            return Err(DesktopError::rejected(
                "Disconnect Companion before applying firmware",
            ));
        }
        // Service::install validates before starting its worker. An Err here is
        // a known rejection; IPC/worker delivery failures use unknown instead.
        self.updater
            .install(request)
            .map_err(DesktopError::rejected)
    }
    fn discover_update(&self) -> Reply {
        let guard = self
            .companion
            .lock()
            .map_err(|_| DesktopError::unknown("The device coordinator stopped"))?;
        if guard.is_open() {
            return Err(DesktopError::rejected(
                "Disconnect Companion before detecting the update interface",
            ));
        }
        self.updater.devices().map_err(DesktopError::rejected)
    }
    fn shutdown(&self) {
        if let Ok(mut guard) = self.companion.lock() {
            guard.shutdown();
        }
    }
}

struct PendingInstall(Arc<DesktopState>);
impl Drop for PendingInstall {
    fn drop(&mut self) {
        self.0.pending_installs.fetch_sub(1, Ordering::AcqRel);
    }
}

fn trusted_url(url: &tauri::Url) -> bool {
    url.username().is_empty()
        && url.password().is_none()
        && url.port().is_none()
        && (url.host_str() == Some("localhost") && url.scheme() == "tauri"
            || url.host_str() == Some("tauri.localhost")
                && matches!(url.scheme(), "http" | "https"))
}
fn bootstrap_url(url: &tauri::Url) -> bool {
    url.as_str() == "about:blank"
}
// A visible monitor stays useful beside a DAW. Focus loss does not suspend it.
fn companion_active(visible: bool, minimized: bool) -> bool {
    visible && !minimized
}
fn trusted_blob(url: &tauri::Url) -> bool {
    url.scheme() == "blob"
        && url
            .path()
            .parse::<tauri::Url>()
            .is_ok_and(|origin| trusted_url(&origin))
}
fn allowed_export(url: &tauri::Url, destination: &Path) -> bool {
    trusted_blob(url) && exports::valid_export_name(destination)
}
fn authorize(window: &tauri::WebviewWindow) -> Reply<()> {
    if window.label() != "main"
        || !trusted_url(
            &window
                .url()
                .map_err(|_| DesktopError::rejected("Could not verify the application window"))?,
        )
    {
        Err(DesktopError::rejected(
            "Device access is restricted to the bundled main application",
        ))
    } else {
        Ok(())
    }
}
async fn background<T: Send + 'static>(
    task: impl FnOnce() -> Reply<T> + Send + 'static,
) -> Reply<T> {
    tauri::async_runtime::spawn_blocking(task)
        .await
        .map_err(|_| {
            DesktopError::unknown(
                "The native task stopped; inspect its current status before retrying",
            )
        })?
}

#[tauri::command]
fn desktop_info(window: tauri::WebviewWindow, state: tauri::State<'_, Arc<DesktopState>>) -> Reply {
    authorize(&window)?;
    let mut info = state.info();
    info["active"] = json!(companion_active(
        window.is_visible().unwrap_or(false),
        window.is_minimized().unwrap_or(true)
    ));
    info["window"] =
        json!({"decorations":false,"transparent":cfg!(any(target_os="windows",target_os="macos")),"resizable":true});
    Ok(info)
}
#[tauri::command]
fn window_minimize(window: tauri::WebviewWindow) -> Reply<()> {
    authorize(&window)?;
    window
        .minimize()
        .map_err(|_| DesktopError::rejected("Could not minimize the window"))
}
#[tauri::command]
fn window_toggle_maximize(window: tauri::WebviewWindow) -> Reply<()> {
    authorize(&window)?;
    if window
        .is_maximized()
        .map_err(|_| DesktopError::rejected("Could not read the window state"))?
    {
        window
            .unmaximize()
            .map_err(|_| DesktopError::rejected("Could not restore the window"))
    } else {
        window
            .maximize()
            .map_err(|_| DesktopError::rejected("Could not maximize the window"))
    }
}
#[tauri::command]
fn window_is_maximized(window: tauri::WebviewWindow) -> Reply {
    authorize(&window)?;
    Ok(
        json!({"maximized":window.is_maximized().map_err(|_|DesktopError::rejected("Could not read the window state"))?}),
    )
}
#[tauri::command]
fn window_begin_drag(window: tauri::WebviewWindow) -> Reply<()> {
    authorize(&window)?;
    let diagnostics = window
        .try_state::<Arc<DesktopState>>()
        .is_some_and(|state| state.diagnostics.load(Ordering::Acquire));
    if diagnostics {
        eprintln!(
            "desktop drag-request maximized={} resizable={}",
            window.is_maximized().unwrap_or(false),
            window.is_resizable().unwrap_or(false)
        );
    }
    let result = window
        .start_dragging()
        .map_err(|_| DesktopError::rejected("Could not move the window"));
    if diagnostics {
        eprintln!("desktop drag-queued success={}", result.is_ok());
    }
    result
}
fn resize_variant(direction: &str) -> Reply<&'static str> {
    match direction {
        "n" => Ok("North"),
        "ne" => Ok("NorthEast"),
        "e" => Ok("East"),
        "se" => Ok("SouthEast"),
        "s" => Ok("South"),
        "sw" => Ok("SouthWest"),
        "w" => Ok("West"),
        "nw" => Ok("NorthWest"),
        _ => Err(DesktopError::rejected("Unknown window resize direction")),
    }
}
#[tauri::command]
async fn window_begin_resize(window: tauri::WebviewWindow, direction: String) -> Reply<()> {
    authorize(&window)?;
    let diagnostics = window
        .try_state::<Arc<DesktopState>>()
        .is_some_and(|state| state.diagnostics.load(Ordering::Acquire));
    if diagnostics {
        eprintln!(
            "desktop resize-request direction={direction} maximized={} resizable={}",
            window.is_maximized().unwrap_or(false),
            window.is_resizable().unwrap_or(false)
        );
    }
    if !window.is_resizable().unwrap_or(false) || window.is_maximized().unwrap_or(true) {
        return Err(DesktopError::rejected(
            "The window cannot be resized in its current state",
        ));
    }
    let native_direction = resize_variant(&direction)?;
    #[cfg(target_os = "windows")]
    let result = windowing::begin_resize(window.clone(), native_direction).await;
    #[cfg(not(target_os = "windows"))]
    let direction = serde_json::from_value(json!(native_direction))
        .map_err(|_| DesktopError::rejected("Invalid native resize direction"))?;
    #[cfg(not(target_os = "windows"))]
    let result = window
        .as_ref()
        .window()
        .start_resize_dragging(direction)
        .map_err(|_| DesktopError::rejected("Could not resize the window"));
    if diagnostics {
        eprintln!("desktop resize-queued success={}", result.is_ok());
    }
    result
}
#[tauri::command]
fn window_close(window: tauri::WebviewWindow) -> Reply<()> {
    authorize(&window)?;
    // close() emits CloseRequested, preserving the active-update exit guard.
    window
        .close()
        .map_err(|_| DesktopError::rejected("Could not request window close"))
}
#[tauri::command]
async fn companion_list(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
) -> Reply {
    authorize(&window)?;
    let state = state.inner().clone();
    background(move || state.list()).await
}
#[tauri::command]
async fn companion_open(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    device_id: String,
) -> Reply {
    authorize(&window)?;
    let state = state.inner().clone();
    background(move || state.open(&device_id)).await
}
#[tauri::command]
async fn companion_read(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    session: String,
) -> Reply {
    authorize(&window)?;
    let connection = state.session(&session)?;
    background(move || connection.read().map_err(DesktopError::unknown)).await
}
#[tauri::command]
async fn companion_write(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    session: String,
    bytes: Vec<u8>,
) -> Reply<()> {
    authorize(&window)?;
    companion::validate_command(&bytes)?;
    let connection = state.session(&session)?;
    background(move || connection.write(&bytes).map_err(DesktopError::unknown)).await
}
#[tauri::command]
async fn companion_close(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    session: String,
) -> Reply<()> {
    authorize(&window)?;
    let state = state.inner().clone();
    background(move || state.close(&session)).await
}
#[tauri::command]
async fn companion_exchange(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    session: String,
    command: String,
) -> Reply {
    authorize(&window)?;
    let connection = state.session(&session)?;
    background(move || {
        connection
            .exchange_many(&[command])
            .map(|lines| json!({"line":lines[0]}))
            .map_err(DesktopError::unknown)
    })
    .await
}
#[tauri::command]
async fn companion_exchange_many(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    session: String,
    commands: Vec<String>,
) -> Reply {
    authorize(&window)?;
    let connection = state.session(&session)?;
    background(move || {
        connection
            .exchange_many(&commands)
            .map(|lines| json!({"lines":lines}))
            .map_err(DesktopError::unknown)
    })
    .await
}
#[tauri::command]
async fn updater_status(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
) -> Reply {
    authorize(&window)?;
    let service = state.updater.clone();
    background(move || Ok(service.status())).await
}
#[tauri::command]
async fn updater_releases(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
) -> Reply {
    authorize(&window)?;
    let service = state.updater.clone();
    background(move || Ok(service.releases())).await
}
#[tauri::command]
async fn updater_devices(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
) -> Reply {
    authorize(&window)?;
    let state = state.inner().clone();
    background(move || state.discover_update()).await
}
#[tauri::command]
async fn updater_progress(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    operation_id: String,
    after_sequence: Option<u64>,
) -> Reply {
    authorize(&window)?;
    let _ = after_sequence;
    let service = state.updater.clone();
    background(move || {
        service
            .progress(&operation_id)
            .map_err(DesktopError::rejected)
    })
    .await
}
#[tauri::command]
async fn updater_inspect(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    bytes: Vec<u8>,
    name: String,
    expected_build: Option<String>,
) -> Reply {
    authorize(&window)?;
    if bytes.len() > package::PACKAGE_BYTES_MAX
        || name.len() > 255
        || name.contains(['\\', '/', '\0'])
    {
        return Err(DesktopError::rejected(
            "Choose a bounded firmware package with a plain filename",
        ));
    }
    let service = state.updater.clone();
    background(move || {
        service
            .inspect(bytes, &name, expected_build.as_deref())
            .map_err(DesktopError::rejected)
    })
    .await
}
#[tauri::command]
async fn updater_select_release(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    id: String,
) -> Reply {
    authorize(&window)?;
    let service = state.updater.clone();
    background(move || service.select_release(&id).map_err(DesktopError::rejected)).await
}
#[tauri::command]
async fn updater_install(
    window: tauri::WebviewWindow,
    state: tauri::State<'_, Arc<DesktopState>>,
    request: Value,
) -> Reply {
    authorize(&window)?;
    let state = state.inner().clone();
    state.pending_installs.fetch_add(1, Ordering::AcqRel);
    let pending = PendingInstall(state.clone());
    background(move || {
        let _pending = pending;
        state.install(&request)
    })
    .await
}

pub fn run(simulation: bool, diagnostics: bool) -> Result<(), String> {
    let app=tauri::Builder::default()
        .setup(move|app|{
            if diagnostics{eprintln!("desktop setup");}
            // A local macOS Cargo build is an unbundled executable. Tauri cannot
            // resolve bundle resources there; use only its sibling resources.
            let resource_root=match app.path().resource_dir(){
                Ok(root)=>root,
                Err(_)=>std::env::current_exe()?.parent()
                    .ok_or("Desktop executable has no parent directory")?.to_path_buf(),
            };
            let catalog=resource_root.join("resources/releases.json");
            let state=Arc::new(DesktopState::new(simulation,catalog.is_file().then_some(catalog))?);
            state.diagnostics.store(diagnostics,Ordering::Release);
            app.manage(state);
            let builder=tauri::WebviewWindowBuilder::from_config(app,&app.config().app.windows[0])?;
            #[cfg(any(target_os="windows",target_os="macos"))]
            let builder=builder.transparent(true).background_color(tauri::window::Color(0,0,0,0));
            #[cfg(target_os="windows")]
            let builder=builder.no_redirection_bitmap(true);
            let window=builder
                .on_navigation(move|url|{
                    let allowed=trusted_url(url)||bootstrap_url(url);
                    if diagnostics{eprintln!("desktop navigation scheme={} host={} allowed={allowed}",url.scheme(),url.host_str().unwrap_or("none"));}
                    allowed
                })
                .on_new_window(|_,_|tauri::webview::NewWindowResponse::Deny)
                .on_download(|webview,event|{
                    if webview.label()!="main"||!webview.url().is_ok_and(|url|trusted_url(&url)){return false;}
                    match event{
                        tauri::webview::DownloadEvent::Requested{url,destination}=>{
                            let target=allowed_export(&url,destination).then(||exports::unique_destination(destination)).transpose();
                            let allowed=match target{Ok(Some(path))=>{*destination=path;true},_=>false};
                            if !allowed{let _=webview.emit("desktop-download",json!({"url":url.as_str(),"success":false}));}
                            allowed
                        },
                        tauri::webview::DownloadEvent::Finished{url,success,..}=>{
                            if trusted_blob(&url){let _=webview.emit("desktop-download",json!({"url":url.as_str(),"success":success}));}
                            true
                        },
                        _=>false
                    }
                })
                .build()?;
            if diagnostics{eprintln!("desktop window-created visible={} minimized={}",window.is_visible().unwrap_or(false),window.is_minimized().unwrap_or(false));}
            Ok(())
        })
        .on_page_load(move|window,payload|{
            if diagnostics{eprintln!("desktop page-load {:?} scheme={} host={}",payload.event(),payload.url().scheme(),payload.url().host_str().unwrap_or("none"));}
            if bootstrap_url(payload.url()){return;}
            if window.label()!="main"||!trusted_url(payload.url()){let _=window.close();return;}
            if payload.event()==tauri::webview::PageLoadEvent::Started{
                if let Some(state)=window.try_state::<Arc<DesktopState>>(){state.shutdown();}
            }
        })
        .on_window_event(|window,event|{
            let Some(state)=window.try_state::<Arc<DesktopState>>() else{return;};
            match event{
                tauri::WindowEvent::CloseRequested{api,..} if state.updating()=>{
                    api.prevent_close();let _=window.emit("desktop-close-blocked",json!({"reason":"An update is still active. Keep THY666 open until its status is resolved."}));
                },
                tauri::WindowEvent::CloseRequested{..}=>state.shutdown(),
                tauri::WindowEvent::Focused(_)=>{let _=window.emit("desktop-activity",json!({"active":companion_active(window.is_visible().unwrap_or(false),window.is_minimized().unwrap_or(true))}));},
                tauri::WindowEvent::Resized(_)=>{
                    let _=window.emit("desktop-activity",json!({"active":companion_active(window.is_visible().unwrap_or(false),window.is_minimized().unwrap_or(true))}));
                    let _=window.emit("desktop-window-state",json!({"maximized":window.is_maximized().unwrap_or(false)}));
                },
                _=>{}
            }
        })
        .invoke_handler(tauri::generate_handler![desktop_info,window_minimize,window_toggle_maximize,window_is_maximized,window_begin_drag,window_begin_resize,window_close,companion_list,companion_open,companion_read,companion_write,
            companion_close,companion_exchange,companion_exchange_many,updater_status,updater_releases,updater_devices,updater_progress,updater_inspect,updater_select_release,updater_install])
        .build(tauri::generate_context!()).map_err(|_|"Could not initialize the THY666 desktop window")?;
    app.run(move |handle, event| match event {
        tauri::RunEvent::ExitRequested { api, .. } => {
            if diagnostics {
                eprintln!("desktop exit-requested");
            }
            let state = handle.state::<Arc<DesktopState>>();
            if state.updating() {
                api.prevent_exit();
            } else {
                state.shutdown();
            }
        }
        tauri::RunEvent::Exit => {
            if diagnostics {
                eprintln!("desktop exit");
            }
            handle.state::<Arc<DesktopState>>().shutdown();
        }
        _ => {}
    });
    Ok(())
}

/// Headless smoke checks exercise fake CDC bytes and the reusable update service
/// without starting a window, enumerating interfaces or opening hardware.
pub fn self_test() -> Result<Value, String> {
    let state = DesktopState::new(true, None)?;
    let devices = state.list().map_err(|e| e.message)?;
    let opened = state
        .open(
            devices["devices"][0]["id"]
                .as_str()
                .ok_or("Missing simulation interface")?,
        )
        .map_err(|e| e.message)?;
    let session = opened["session"]
        .as_str()
        .ok_or("Missing simulation session")?;
    let connection = state.session(session).map_err(|e| e.message)?;
    let lines = connection.exchange_many(&["status".into()])?;
    if !lines[0].contains("ready=0") {
        return Err("Simulation claimed a healthy physical instrument".into());
    }
    if state.discover_update().is_ok() {
        return Err("Update discovery ignored Companion ownership".into());
    }
    state.close(session).map_err(|e| e.message)?;
    let update = state.discover_update().map_err(|e| e.message)?;
    if update["simulation"] != true {
        return Err("Updater did not stay simulated".into());
    }
    Ok(
        json!({"protocol":PROTOCOL,"simulation":true,"hardware_accessed":false,"checks":["explicit-cdc","bounded-exchange","exclusive-usb","simulated-updater"]}),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn construction_is_idle_and_fake_smoke_checks_avoid_native_usb() {
        let state = DesktopState::new(true, None).unwrap();
        assert!(!state.updating());
        assert_eq!(state.updater.status()["operation"], Value::Null);
        assert_eq!(self_test().unwrap()["hardware_accessed"], false);
    }
    #[test]
    fn visible_companion_stays_live_beside_a_daw_and_only_hidden_or_minimized_suspends() {
        assert!(companion_active(true, false));
        assert!(!companion_active(false, false));
        assert!(!companion_active(true, true));
        assert!(!companion_active(false, true));
    }
    #[test]
    fn custom_window_resize_only_accepts_the_eight_named_edges_and_corners() {
        for (short, long) in [
            ("n", "North"),
            ("ne", "NorthEast"),
            ("e", "East"),
            ("se", "SouthEast"),
            ("s", "South"),
            ("sw", "SouthWest"),
            ("w", "West"),
            ("nw", "NorthWest"),
        ] {
            assert_eq!(resize_variant(short).unwrap(), long);
        }
        for value in ["", "north", "North", "n\0", "resize", "n/e"] {
            assert!(resize_variant(value).is_err());
        }
    }
    #[test]
    fn only_embedded_app_origins_are_trusted() {
        for url in [
            "tauri://localhost/index.html",
            "http://tauri.localhost/index.html",
            "https://tauri.localhost/",
        ] {
            assert!(trusted_url(&url.parse().unwrap()));
        }
        assert!(bootstrap_url(&"about:blank".parse().unwrap()));
        assert!(!trusted_url(&"about:blank".parse().unwrap()));
        assert!(!bootstrap_url(&"about:blank#untrusted".parse().unwrap()));
        for url in [
            "https://example.invalid/",
            "http://127.0.0.1:3330/",
            "file:///tmp/index.html",
            "tauri://attacker/index.html",
            "https://tauri.localhost.evil/",
            "http://tauri.localhost:3330/",
            "http://other@tauri.localhost/",
        ] {
            assert!(!trusted_url(&url.parse().unwrap()));
        }
    }
    #[test]
    fn pending_native_install_blocks_window_exit_until_the_dispatch_guard_drops() {
        let state = Arc::new(DesktopState::new(true, None).unwrap());
        state.pending_installs.fetch_add(1, Ordering::AcqRel);
        let guard = PendingInstall(state.clone());
        assert!(state.updating());
        assert!(state.list().is_err());
        drop(guard);
        assert!(!state.updating());
        assert!(state.list().is_ok());
    }
    #[test]
    fn downloads_only_accept_bundled_origin_blobs_and_known_export_extensions() {
        for suffix in ["thyb", "thyl", "thys", "json", "mid", "midi"] {
            assert!(allowed_export(
                &"blob:http://tauri.localhost/synthetic".parse().unwrap(),
                Path::new(&format!("example.{suffix}"))
            ));
        }
        for url in [
            "https://example.invalid/backup.thyb",
            "blob:https://example.invalid/synthetic",
            "blob:http://tauri.localhost:9999/synthetic",
            "file:///synthetic.thyb",
        ] {
            assert!(!allowed_export(
                &url.parse().unwrap(),
                Path::new("example.thyb")
            ));
        }
        assert!(!allowed_export(
            &"blob:http://tauri.localhost/synthetic".parse().unwrap(),
            Path::new("example.exe")
        ));
    }
    #[test]
    fn active_companion_rejects_update_and_stale_session_cannot_write() {
        let state = DesktopState::new(true, None).unwrap();
        state.list().unwrap();
        let open = state.open("simulation-cdc").unwrap();
        let id = open["session"].as_str().unwrap();
        assert_eq!(state.install(&json!({})).unwrap_err().kind, "rejected");
        assert!(state.discover_update().is_err());
        state.close(id).unwrap();
        assert!(state.session(id).is_err());
    }
}
