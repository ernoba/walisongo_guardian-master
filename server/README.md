# Walisongo Guardian Server v3.1

Secure data collection and monitoring server for Walisongo Guardian client application.

## Features

✨ **Comprehensive Data Management**
- Structured file storage with hierarchical organization
- Complete metadata tracking for each file
- SHA256 integrity verification for all data
- Version tracking and client compatibility management

🔐 **Security**
- API key authentication for all endpoints
- XOR encryption for data transmission
- Encrypted data stored securely
- Access control and audit logging

📊 **Analytics & Query**
- Query files by user, type, or date
- Server statistics and metrics
- Real-time monitoring capabilities
- Structured logging with JSONL format

🚀 **Performance**
- Chunked upload support for large files
- Asynchronous background processing
- Efficient file indexing system
- In-memory index with disk persistence

---

## Installation & Setup

### Prerequisites
- Rust 1.70+ with Cargo
- 2GB RAM minimum
- Linux/macOS/Windows with network access

### Build

```bash
cd server
cargo build --release
```

### Configuration

1. Copy `.env.example` ke `.env`:
```bash
cp .env.example .env
```

2. Edit `.env` dengan konfigurasi Anda:
```bash
nano .env
```

3. Pastikan direktori uploads dan updates ada:
```bash
mkdir -p uploads updates
```

### Run

**Development mode:**
```bash
cargo run
```

**Production mode:**
```bash
cargo run --release
```

Server akan mendengarkan di `0.0.0.0:8080` secara default.

---

## Directory Structure

```
server/
├── src/
│   └── main.rs                 # Main server code (fully featured)
├── Cargo.toml                  # Rust dependencies
├── .env.example               # Configuration template
├── README.md                  # This file
└── uploads/                   # Data storage root
    ├── _index.json            # Central file index
    ├── Class1/
    │   ├── Student1/
    │   │   ├── screenshot/
    │   │   │   └── 2024-04-28/
    │   │   │       └── 14-30-45/
    │   │   │           ├── FILE_ID__image1.jpg
    │   │   │           └── FILE_ID__image1.json
    │   │   ├── keylog/
    │   │   └── _active_status.jsonl
    │   └── Student2/
    └── ...
```

---

## Data Organization

### File Storage Hierarchy
```
uploads/
  {CLASS}/                      # Kelas/grup
    {STUDENT_NAME}/             # Nama siswa
      {DATA_TYPE}/              # Jenis data (screenshot, keylog, dll)
        {YYYY-MM-DD}/           # Tanggal
          {HH-MM-SS}/           # Waktu
            FILE_ID__filename.ext      # File asli (dengan extension)
            FILE_ID__filename.json     # Metadata JSON
```

### File Metadata JSON Structure

Setiap file memiliki companion JSON file dengan metadata lengkap:

```json
{
  "file_id": "20240428_143045_123_a1b2c3d4",
  "original_filename": "screenshot.png",
  "data_type": "screenshot",
  "user_name": "Ahmad",
  "user_class": "XI-A",
  "client_version": "1.6.3",
  "received_at": "2024-04-28T14:30:45.123+07:00",
  "client_timestamp": "2024-04-28T14:30:30",
  "file_size": 102400,
  "file_hash": "abc123def456...",
  "status": "complete",
  "stored_path": "/path/to/file.jpg",
  "client_ip": "192.168.1.100",
  "extra_metadata": {
    "application": "VLC",
    "window_title": "Movie",
    "monitor_count": "1"
  }
}
```

### Active Status Log Format

File `_active_status.jsonl` menyimpan setiap heartbeat dalam format JSON Lines:

```jsonl
{"client_version":"1.6.3","wifi_ssid":"MyWiFi","wifi_password":"***","received_at":"2024-04-28T14:30:45.123+07:00","client_ip":"192.168.1.100","user_name":"Ahmad","user_class":"XI-A"}
{"client_version":"1.6.3","wifi_ssid":"MyWiFi","wifi_password":"***","received_at":"2024-04-28T14:40:45.456+07:00","client_ip":"192.168.1.100","user_name":"Ahmad","user_class":"XI-A"}
```

---

## API Endpoints

### Authentication
Semua endpoint (kecuali `/health`) memerlukan header `X-API-Key`:

```bash
curl -H "X-API-Key: ADMIN-2025" http://localhost:8080/endpoint
```

### Upload Endpoints

#### POST /upload
Upload file dengan chunked transfer support.

**Headers:**
```
X-API-Key: ADMIN-2025
X-User: Student Name[Class]
X-Type: screenshot|keylog|activity|video|audio|custom
X-File-ID: unique_file_identifier
X-Filename: original_filename.ext
X-Chunk-Num: 0                          (current chunk number)
X-Chunk-Total: 5                        (total chunks)
X-Timestamp: 2024-04-28T14:30:30
X-Version: 1.6.3
Content-Type: application/octet-stream
```

**Body:** Binary file content chunk

**Response:**
```
HTTP/1.1 200 OK
```

**Example (curl):**
```bash
curl -X POST http://localhost:8080/upload \
  -H "X-API-Key: ADMIN-2025" \
  -H "X-User: Ahmad[XI-A]" \
  -H "X-Type: screenshot" \
  -H "X-File-ID: file_001" \
  -H "X-Filename: screen.png" \
  -H "X-Chunk-Num: 0" \
  -H "X-Chunk-Total: 1" \
  -H "X-Version: 1.6.3" \
  --data-binary @screenshot.png
```

#### POST /post-active
Send client status dan WiFi information (heartbeat).

**Headers:**
```
X-API-Key: ADMIN-2025
X-User: Student Name[Class]
X-Client-IP: 192.168.1.100
Content-Type: application/octet-stream
```

**Body:** Encrypted data format: `version|ssid|password`

**Response:**
```
HTTP/1.1 200 OK
```

### Update Endpoints

#### GET /check-update
Check if update available dan get latest version hash.

**Query Parameters:** None

**Response:**
```json
{
  "latest_hash": "abc123def456789...",
  "version": "1.6.3",
  "status": "success"
}
```

#### GET /get-update
Download latest executable binary.

**Response:**
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
[Binary data]
```

### Query Endpoints

#### GET /api/files/{user}/{class}
Query semua file dari user tertentu.

**Response:**
```json
{
  "user": "Ahmad",
  "class": "XI-A",
  "file_count": 42,
  "files": [
    {
      "file_id": "20240428_143045_123_a1b2c3d4",
      "original_filename": "screenshot.png",
      "data_type": "screenshot",
      ...
    }
  ]
}
```

#### GET /api/files/type/{file_type}
Query semua file dari tipe tertentu (screenshot, keylog, dll).

**Response:**
```json
{
  "type": "screenshot",
  "file_count": 156,
  "files": [...]
}
```

#### GET /api/files/date/{date}
Query semua file dari tanggal tertentu (format: YYYY-MM-DD).

**Response:**
```json
{
  "date": "2024-04-28",
  "file_count": 89,
  "files": [...]
}
```

#### GET /api/files/{file_id}/metadata
Get metadata lengkap untuk file tertentu.

**Response:**
```json
{
  "file_id": "20240428_143045_123_a1b2c3d4",
  "original_filename": "screenshot.png",
  "data_type": "screenshot",
  "user_name": "Ahmad",
  "user_class": "XI-A",
  "client_version": "1.6.3",
  "received_at": "2024-04-28T14:30:45.123+07:00",
  "file_size": 102400,
  "file_hash": "abc123def456...",
  "stored_path": "/path/to/file.jpg"
}
```

#### GET /api/stats
Get server statistics dan metrics.

**Response:**
```json
{
  "total_files": 2148,
  "total_users": 45,
  "total_data_types": 5,
  "total_size_bytes": 5368709120,
  "total_size_mb": 5120.0,
  "type_distribution": {
    "screenshot": 1200,
    "keylog": 600,
    "activity": 348,
    "video": 0,
    "audio": 0
  },
  "index_last_updated": "2024-04-28T14:45:30.123+07:00"
}
```

### Health Check

#### GET /health
Simple health check endpoint.

**Response:**
```json
{
  "status": "healthy",
  "version": "3.1",
  "timestamp": "2024-04-28T14:45:30.123+07:00"
}
```

---

## Data Flow

### Upload Flow

```
Client 
  ↓ (format: version|ssid|password)
  ↓ (encrypt with XOR)
  ↓ (chunk data if large)
  ↓
Server /upload
  ↓ (receive chunks)
  ↓ (validate auth & headers)
  ↓ (write to temp file)
  ↓ (all chunks received?)
    └─→ finalize_file_task (async)
        ├─ read encrypted file
        ├─ decrypt with static key
        ├─ save with correct extension
        ├─ create metadata JSON
        ├─ update file index
        ├─ save index to disk
        └─ clean up temp file
```

### Active Status Flow

```
Client
  ↓ (collect SSID & password)
  ↓ (format: version|ssid|password)
  ↓ (encrypt with XOR)
  ↓
Server /post-active
  ├─ validate auth
  ├─ decrypt data
  ├─ parse version|ssid|password
  ├─ save to JSONL log
  ├─ save to human-readable log
  └─ respond OK
```

---

## Security Considerations

### Encryption Keys

**Static Key** (XOR Cipher):
```
WALISONGO_JAYA_SELAMANYA
```

**API Key**:
```
ADMIN-2025
```

⚠️ **Production Security:**
- Change both keys immediately after setup
- Use HTTPS/TLS for all communications
- Implement rate limiting
- Use strong API keys (minimum 32 characters)
- Store keys in environment variables, never in code

### File Integrity

Setiap file menyimpan SHA256 hash untuk verification:

```bash
# Verify integrity
sha256sum uploaded_file.jpg
# Compare dengan field "file_hash" di metadata JSON
```

### Access Control

Implementasi endpoint `/api/files/*` memerlukan `X-API-Key` header. Pertimbangkan:
- Role-based access control (RBAC)
- Rate limiting per API key
- Audit logging untuk setiap akses

---

## Monitoring & Analytics

### Query Examples

**Semua file dari Ahmad, kelas XI-A:**
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/Ahmad/XI-A
```

**Semua screenshot dari hari ini:**
```bash
curl -H "X-API-Key: ADMIN-2025" \
  "http://localhost:8080/api/files/date/$(date +%Y-%m-%d)"
```

**Server statistics:**
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/stats
```

**Metadata file spesifik:**
```bash
curl -H "X-API-Key: ADMIN-2025" \
  "http://localhost:8080/api/files/20240428_143045_123_a1b2c3d4/metadata"
```

### Log Analysis

**View recent heartbeats:**
```bash
tail -20 uploads/XI-A/Ahmad/_active_status.jsonl
```

**Find all files from specific date:**
```bash
find uploads -type f -newermt "2024-04-28" -path "*/2024-04-28/*"
```

**Calculate total data collected:**
```bash
du -sh uploads/
```

---

## Troubleshooting

### Server Won't Start

1. Check port 8080 is available:
```bash
lsof -i :8080
```

2. Check directory permissions:
```bash
ls -la uploads/
ls -la updates/
```

3. Check logs:
```bash
RUST_LOG=debug cargo run
```

### Files Not Being Saved

1. Verify API key matches (`X-API-Key` header)
2. Check encryption key matches client (`STATIC_ENCRYPTION_KEY`)
3. Verify write permissions on `uploads/` directory
4. Check disk space: `df -h`

### Can't Download Update

1. Verify `WinSysHelper.exe` exists in `updates/` directory
2. Check file permissions: `ls -la updates/`
3. Verify `API_AUTH_KEY` header in request

---

## Performance Tips

1. **Periodic Index Backup:**
```bash
cp uploads/_index.json uploads/_index.json.backup
```

2. **Archive Old Data:**
```bash
find uploads -type f -mtime +30 -exec tar -czf archive.tar.gz {} \;
```

3. **Monitor Disk Usage:**
```bash
watch -n 60 'du -sh uploads/'
```

4. **Enable JSON Logging for Analytics:**
```bash
# Set LOG_JSON=true in .env
```

---

## Future Enhancements

- [ ] SQLite database for advanced querying
- [ ] Advanced AES-GCM encryption
- [ ] Rate limiting per API key
- [ ] Web dashboard for monitoring
- [ ] Data export (CSV, Excel)
- [ ] Machine learning for anomaly detection
- [ ] Clustering support for scalability
- [ ] Backup/restore utilities

---

## Support

For issues atau questions:
1. Check logs: `RUST_LOG=debug cargo run`
2. Verify configuration in `.env`
3. Review API documentation above
4. Check client version compatibility

---

## Version History

### v3.1
- ✅ Full metadata tracking for each file
- ✅ Structured file indexing system
- ✅ Query endpoints untuk analytics
- ✅ Active status dengan version tracking
- ✅ Comprehensive logging (JSONL + human-readable)
- ✅ SHA256 integrity verification
- ✅ Better directory organization

### v3.0
- Update check dan download
- Basic file upload

---

## License

PROPRIETARY - Walisongo Guardian Team
