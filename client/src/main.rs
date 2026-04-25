use reqwest::Client;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// --- KONFIGURASI IDENTIK ---
const CLIENT_VERSION: &str = "1.6.2";
const ENCRYPTION_KEY: &str = "WALISONGO_JAYA_SELAMANYA";
const RAW_API_KEY: &str = "ADMIN-2025";
const CHUNK_SIZE: usize = 512 * 1024;

fn xor_data(data: &mut Vec<u8>) {
    let mut key: u8 = 0x53;
    for i in 0..data.len() {
        data[i] ^= key;
        key = key.wrapping_add(i as u8);
    }
}

fn final_encryption(data: &mut Vec<u8>) {
    let key_bytes = ENCRYPTION_KEY.as_bytes();
    for i in 0..data.len() {
        data[i] ^= key_bytes[i % key_bytes.len()];
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("🛡️  Walisongo Guardian - Smart Load Tester v3.0 (Ramp-Up Mode)");
    println!("--------------------------------------------------------------\n");

    // ==========================================
    // ⚙️ KONFIGURASI SMART RAMP-UP
    // ==========================================
    let users_per_batch = 10;       // Tambah 10 user setiap step
    let ramp_up_delay = 3;          // Tunggu 3 detik sebelum menambah user lagi
    let request_delay_ms = 100;     // Jeda per user untuk mengirim file (jangan terlalu kecil agar klien tidak crash)
    let error_threshold = 20;       // Berapa banyak error beruntun/gagal agar test berhenti
    // ==========================================

    // Timeout diperpendek agar cepat mendeteksi jika server nge-hang
    let client = Client::builder()
        .timeout(Duration::from_secs(5)) 
        .build()?;
    let server_url = "http://localhost:8080/upload";

    println!("🌐 Mengambil payload (gambar random) 1 kali...");
    let response = client.get("https://picsum.photos/500/500").send().await?;
    let mut payload = response.bytes().await?.to_vec();
    
    let original_size = payload.len();
    xor_data(&mut payload);
    final_encryption(&mut payload);
    let shared_payload = Arc::new(payload);
    
    println!("📦 Payload siap: {} bytes", original_size);
    println!("🚀 MEMULAI SERANGAN BERTANGHAP (RAMP-UP)...");
    println!("Metode: Menambah {} user setiap {} detik sampai server kewalahan.\n", users_per_batch, ramp_up_delay);

    let is_running = Arc::new(AtomicBool::new(true));
    let active_users = Arc::new(AtomicUsize::new(0));
    let total_success = Arc::new(AtomicUsize::new(0));
    let total_failed = Arc::new(AtomicUsize::new(0));
    let total_bytes_sent = Arc::new(AtomicUsize::new(0));
    
    let start_time = Instant::now();

    // --- TASK MONITORING (Mendeteksi Kematian Server) ---
    let is_running_monitor = Arc::clone(&is_running);
    let failed_monitor = Arc::clone(&total_failed);
    let users_monitor = Arc::clone(&active_users);
    
    tokio::spawn(async move {
        let mut last_fail_count = 0;
        loop {
            tokio::time::sleep(Duration::from_secs(1)).await;
            if !is_running_monitor.load(Ordering::Relaxed) { break; }

            let current_fails = failed_monitor.load(Ordering::Relaxed);
            let fail_spike = current_fails.saturating_sub(last_fail_count);

            // Jika dalam 1 detik terjadi lonjakan error melebihi threshold, server dianggap tewas
            if fail_spike > error_threshold {
                println!("\n🚨 [ALERT] BATAS AMBANG SERVER DITEMUKAN!");
                println!("🚨 Server mulai menolak koneksi atau Time Out.");
                is_running_monitor.store(false, Ordering::Relaxed);
                break;
            }
            last_fail_count = current_fails;
        }
    });

    // --- TASK SPAWNER (Menambah User Secara Bertahap) ---
    let mut tasks = vec![];
    let mut user_counter = 0;

    while is_running.load(Ordering::Relaxed) {
        println!("📈 Gelombang masuk: Menambah {} user... (Total Aktif: {})", users_per_batch, user_counter + users_per_batch);
        
        for _ in 0..users_per_batch {
            user_counter += 1;
            active_users.store(user_counter, Ordering::Relaxed);

            let client_clone = client.clone();
            let payload_clone = Arc::clone(&shared_payload);
            let success_ref = Arc::clone(&total_success);
            let fail_ref = Arc::clone(&total_failed);
            let bytes_ref = Arc::clone(&total_bytes_sent);
            let running_ref = Arc::clone(&is_running);

            let task = tokio::spawn(async move {
                let user_id = user_counter;
                let student_name = format!("Siswa Virtual {}", user_id);
                let student_class = format!("Kelas-{}", (user_id % 3) + 10);
                let system_id = format!("SMART-NODE-{:03}", user_id);

                // User akan terus mengirim file selama tes masih berjalan (is_running == true)
                let mut file_idx = 0;
                while running_ref.load(Ordering::Relaxed) {
                    file_idx += 1;
                    let filename = format!("stress_test_u{}_f{}.jpg", user_id, file_idx);
                    let file_id = format!("{}-{}-{}", SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis(), user_id, file_idx);

                    let total_chunks = (payload_clone.len() + CHUNK_SIZE - 1) / CHUNK_SIZE;
                    let mut file_upload_success = true;

                    for i in 0..total_chunks {
                        if !running_ref.load(Ordering::Relaxed) { break; } // Segera hentikan jika server mati

                        let start = i * CHUNK_SIZE;
                        let end = std::cmp::min(start + CHUNK_SIZE, payload_clone.len());
                        let chunk = payload_clone[start..end].to_vec();
                        let chunk_len = chunk.len();

                        let res = client_clone.post(server_url)
                            .header("X-API-Key", RAW_API_KEY)
                            .header("X-Client-Version", CLIENT_VERSION)
                            .header("X-ID", &system_id)
                            .header("X-User", format!("{} [{}]", student_name, student_class))
                            .header("X-Type", "screenshot")
                            .header("X-Filename", &filename)
                            .header("X-File-ID", &file_id)
                            .header("X-Chunk-Num", i.to_string())
                            .header("X-Chunk-Total", total_chunks.to_string())
                            .header("Content-Type", "application/octet-stream")
                            .body(chunk)
                            .send()
                            .await;

                        match res {
                            Ok(r) if r.status().is_success() => {
                                bytes_ref.fetch_add(chunk_len, Ordering::Relaxed);
                            },
                            _ => {
                                // Gagal (Timeout, Connection Refused, atau 500 Internal Server Error)
                                file_upload_success = false;
                                break; 
                            }
                        }
                        tokio::time::sleep(Duration::from_millis(request_delay_ms)).await;
                    }

                    if file_upload_success && running_ref.load(Ordering::Relaxed) {
                        success_ref.fetch_add(1, Ordering::Relaxed);
                    } else if !file_upload_success {
                        fail_ref.fetch_add(1, Ordering::Relaxed);
                    }
                }
            });
            tasks.push(task);
        }

        // Tunggu beberapa detik sebelum memasukkan gelombang user berikutnya
        // Gunakan loop kecil agar bisa segera di-break jika server tewas saat menunggu
        for _ in 0..ramp_up_delay * 10 {
            if !is_running.load(Ordering::Relaxed) { break; }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }

    println!("\n🔄 Mengumpulkan laporan dari semua entitas klien...");
    
    // Matikan semua flag agar task yang masih tertidur segera bangun dan berhenti
    is_running.store(false, Ordering::Relaxed);
    
    // --- 4. KALKULASI HASIL AKHIR ---
    let elapsed = start_time.elapsed().as_secs_f64();
    let total_mb_sent = total_bytes_sent.load(Ordering::Relaxed) as f64 / 1_048_576.0;
    let speed_mbps = total_mb_sent / elapsed;

    println!("\n===============================================");
    println!("🏁 LAPORAN BATAS AMBANG SERVER (STRESS TEST)");
    println!("===============================================");
    println!("Durasi Tes Bertahan : {:.2} detik", elapsed);
    println!("Kapasitas Maks User : {} Virtual Users", active_users.load(Ordering::Relaxed));
    println!("Total Data Terkirim : {:.2} MB", total_mb_sent);
    println!("Kecepatan Rata-rata : {:.2} MB/detik", speed_mbps);
    println!("-----------------------------------------------");
    println!("File Sukses Masuk   : {}", total_success.load(Ordering::Relaxed));
    println!("File Gagal/Ditolak  : {}", total_failed.load(Ordering::Relaxed));
    println!("===============================================\n");
    println!("💡 Kesimpulan: Server Anda mulai kehilangan stabilitas dan gagal melayani permintaan saat menahan beban sekitar {} user yang mengirim data secara konstan.", active_users.load(Ordering::Relaxed));

    Ok(())
}