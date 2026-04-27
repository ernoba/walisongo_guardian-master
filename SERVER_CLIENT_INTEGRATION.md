# Walisongo Guardian - Client & Server Integration Guide

Complete guide untuk integrasi antara client (Windows) dan server (Linux/Docker).

## Architecture Overview

```
┌─────────────────────────────┐
│  WINDOWS CLIENT             │
│  v1.6.3                     │
│                             │
│ • Screenshot capture        │
│ • Keylogging                │
│ • Activity monitoring       │
│ • WiFi tracking             │
└──────────────┬──────────────┘
               │
               │ HTTPS/TLS
               │ JSON over HTTP
               │
               │ Data Format:
               │ • File: Binary (encrypted)
               │ • Active: version|ssid|password
               │
┌──────────────┴──────────────┐
│  LINUX/DOCKER SERVER        │
│  v3.1                       │
│                             │
│ • Receive & decrypt         │
│ • Organize hierarchically   │
│ • Store metadata            │
│ • Index & query             │
│ • Analytics                 │
└─────────────────────────────┘
```

## Data Flow Sequence

### 1. Upload Flow (File Transfer)

```
CLIENT:
├─ Capture screenshot
├─ Encrypt with XOR (static key)
├─ Split into chunks (512KB each)
└─ Send to server

       POST /upload
       ├─ X-API-Key: ADMIN-2025
       ├─ X-User: Ahmad[XI-A]
       ├─ X-Type: screenshot
       ├─ X-File-ID: 20240428_143045_123_a1b2c3d4
       ├─ X-Filename: screen.png
       ├─ X-Chunk-Num: 0
       ├─ X-Chunk-Total: 3
       ├─ X-Version: 1.6.3
       ├─ X-Timestamp: 2024-04-28T14:30:30
       └─ [Binary encrypted data]

SERVER:
├─ Validate authentication
├─ Extract headers & metadata
├─ Write chunk to temp file
├─ Check if all chunks received
└─ If complete:
   ├─ Decrypt data
   ├─ Determine file type (.jpg)
   ├─ Save with extension
   ├─ Create metadata JSON
   ├─ Calculate SHA256 hash
   ├─ Update index
   ├─ Log activity
   └─ Clean up temp file

DISK STORAGE:
uploads/XI-A/Ahmad/screenshot/2024-04-28/14-30-45/
├── 20240428_143045_123_a1b2__screen.jpg
└── 20240428_143045_123_a1b2__screen.json
```

### 2. Heartbeat Flow (Active Status)

```
CLIENT (every 10 minutes or on WiFi change):
├─ Get current WiFi SSID & password
├─ Format: version|ssid|password
│   Example: 1.6.3|MyWiFi|mypass123
├─ Encrypt with XOR (static key)
└─ Send to server

       POST /post-active
       ├─ X-API-Key: ADMIN-2025
       ├─ X-User: Ahmad[XI-A]
       ├─ X-Client-IP: 192.168.1.100
       └─ [Encrypted data]

SERVER:
├─ Validate authentication
├─ Decrypt data
├─ Parse version|ssid|password
├─ Save to _active_status.jsonl
├─ Save to connection_history.log
└─ Update index with status

DISK STORAGE:
uploads/XI-A/Ahmad/_active_status.jsonl
[One JSON per line with heartbeat data]

uploads/XI-A/Ahmad/connection_history.log
[Human-readable connection history]
```

## Configuration Reference

### Client Configuration (active.h)

```cpp
// In core/config.cpp
namespace Config {
    const std::string CLIENT_VERSION = "1.6.3";
    std::string DYNAMIC_SERVER_URL = "http://your-server:8080";
    const std::string ENCRYPTION_KEY = "WALISONGO_JAYA_SELAMANYA";
    const std::string RAW_API_KEY = "ADMIN-2025";
    
    // Active monitoring: every 30 seconds check, 
    // send only if SSID changed or 10 minutes passed
}
```

### Server Configuration (.env)

```bash
# Server
SERVER_HOST=0.0.0.0
SERVER_PORT=8080

# Paths
UPLOAD_ROOT_PATH=./uploads
UPDATE_ROOT_PATH=./updates

# Security (must match client)
STATIC_ENCRYPTION_KEY=WALISONGO_JAYA_SELAMANYA
API_AUTH_KEY=ADMIN-2025

# Update
UPDATE_EXE_NAME=WinSysHelper.exe
CURRENT_VERSION=1.6.3

# Logging
LOG_LEVEL=INFO
```

## Complete Data Examples

### Example 1: Screenshot Upload

**Request:**
```
POST http://server:8080/upload

Headers:
  X-API-Key: ADMIN-2025
  X-User: Ahmad[XI-A]
  X-Type: screenshot
  X-File-ID: 20240428_143045_123_a1b2c3d4
  X-Filename: window_screen.png
  X-Chunk-Num: 0
  X-Chunk-Total: 2
  X-Version: 1.6.3
  X-Timestamp: 2024-04-28T14:30:30

Body: [Binary encrypted PNG data - 512KB]

---

POST http://server:8080/upload

Headers:
  X-API-Key: ADMIN-2025
  X-User: Ahmad[XI-A]
  X-Type: screenshot
  X-File-ID: 20240428_143045_123_a1b2c3d4
  X-Filename: window_screen.png
  X-Chunk-Num: 1
  X-Chunk-Total: 2
  X-Version: 1.6.3
  X-Timestamp: 2024-04-28T14:30:30

Body: [Binary encrypted PNG data - 256KB (last chunk)]
```

**Server Process:**
```
1. Receives chunk 0 → writes to temp file
2. Receives chunk 1 → writes to temp file
3. Detects all chunks received
4. Async process:
   - Read encrypted file
   - Decrypt with key
   - Save as .jpg
   - Generate metadata JSON
   - Calculate hash
   - Update index
   - Delete temp file
```

**Disk Result:**
```
uploads/XI-A/Ahmad/screenshot/2024-04-28/14-30-45/
├── 20240428_143045_123_a1b2__window_screen.jpg (768KB)
└── 20240428_143045_123_a1b2__window_screen.json
    {
      "file_id": "20240428_143045_123_a1b2c3d4",
      "original_filename": "window_screen.png",
      "data_type": "screenshot",
      "user_name": "Ahmad",
      "user_class": "XI-A",
      "client_version": "1.6.3",
      "received_at": "2024-04-28T14:30:45.123+07:00",
      "file_size": 786432,
      "file_hash": "abc123...",
      "status": "complete",
      "stored_path": "/app/uploads/XI-A/Ahmad/..."
    }
```

### Example 2: Active Status Heartbeat

**Request:**
```
POST http://server:8080/post-active

Headers:
  X-API-Key: ADMIN-2025
  X-User: Ahmad[XI-A]
  X-Client-IP: 192.168.1.100

Body: [Encrypted: "1.6.3|MyWiFi|mypass123"]
```

**Server Decrypts:**
```
1. Receive: [XOR encrypted bytes]
2. Apply XOR decryption
3. Result: "1.6.3|MyWiFi|mypass123"
4. Parse:
   - Version: 1.6.3
   - SSID: MyWiFi
   - Password: mypass123
```

**Disk Result:**
```
uploads/XI-A/Ahmad/_active_status.jsonl
{"client_version":"1.6.3","wifi_ssid":"MyWiFi","wifi_password":"***","received_at":"2024-04-28T14:30:45.123+07:00","client_ip":"192.168.1.100","user_name":"Ahmad","user_class":"XI-A"}

uploads/XI-A/Ahmad/connection_history.log
[2024-04-28 14:30:45] Version: 1.6.3, WiFi: MyWiFi, IP: 192.168.1.100
[2024-04-28 14:40:45] Version: 1.6.3, WiFi: MyWiFi, IP: 192.168.1.100
[2024-04-28 15:10:30] Version: 1.6.3, WiFi: SchoolWiFi, IP: 192.168.2.50
```

## API Usage from Server

### Query User Files
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/Ahmad/XI-A | jq '.'

Response:
{
  "user": "Ahmad",
  "class": "XI-A",
  "file_count": 42,
  "files": [
    {
      "file_id": "20240428_143045_123_a1b2c3d4",
      "original_filename": "window_screen.png",
      "data_type": "screenshot",
      ...
    }
  ]
}
```

### Daily Activity Report
```bash
#!/bin/bash

DATE=$(date +%Y-%m-%d)
API_URL="http://localhost:8080/api"

echo "=== Walisongo Activity Report - $DATE ==="
echo ""

# All files from today
echo "📊 Files from today:"
curl -s -H "X-API-Key: ADMIN-2025" \
  "$API_URL/files/date/$DATE" | jq '.file_count'

echo ""
echo "📸 Screenshot count:"
curl -s -H "X-API-Key: ADMIN-2025" \
  "$API_URL/files/type/screenshot" | jq '.files[] | select(.received_at | contains("'$DATE'")) | .file_id' | wc -l

echo ""
echo "📝 Activity logs:"
curl -s -H "X-API-Key: ADMIN-2025" \
  "$API_URL/stats" | jq '.'
```

## Troubleshooting Integration

### Client Can't Connect

**Client side:**
```cpp
// Check configuration
Config::DYNAMIC_SERVER_URL = "http://server-ip:8080"

// Test connection
if (DYNAMIC_SERVER_URL.empty()) {
    // Server URL not set
}

// Check firewall
// telnet server-ip 8080
```

**Server side:**
```bash
# Check if listening
netstat -tuln | grep 8080

# Check firewall
sudo ufw status

# Check logs
journalctl -u walisongo-guardian -f
```

### Data Not Appearing

**Verify encryption keys match:**
```cpp
// Client
const std::string ENCRYPTION_KEY = "WALISONGO_JAYA_SELAMANYA";

# Server
STATIC_ENCRYPTION_KEY=WALISONGO_JAYA_SELAMANYA
```

**Check API key:**
```cpp
// Client
const std::string RAW_API_KEY = "ADMIN-2025";

# Server
API_AUTH_KEY=ADMIN-2025
```

**Check headers sent:**
```bash
# Enable verbose logging on client
RUST_LOG=debug cargo run
```

### Files Not Being Indexed

```bash
# Check index file
cat uploads/_index.json | jq '.files_by_id | keys'

# Verify metadata files exist
find uploads -name "*.json" | head -5

# Check permissions
ls -la uploads/XI-A/Ahmad/
```

## Performance Tuning

### For Client
```cpp
// Adjust upload chunk size in transmission.cpp
const size_t CHUNK_SIZE = 512 * 1024; // 512 KB

// Adjust heartbeat interval in active.cpp
const int HEARTBEAT_INTERVAL = 590; // 10 minutes
```

### For Server
```bash
# .env
UPLOAD_ROOT_PATH=./uploads
MAX_UPLOAD_SIZE=52428800  # 50MB

# Nginx (if using reverse proxy)
client_max_body_size 100M;
proxy_read_timeout 600s;
proxy_send_timeout 600s;
```

## Migration Scenarios

### Scenario 1: Server Migration

```bash
# 1. Backup old server
./server-utils.sh backup ./migration_backup

# 2. Deploy new server
cargo build --release

# 3. Copy uploads directory
cp -r old_server/uploads/* new_server/uploads/

# 4. Rebuild index
new_server/index_rebuild.sh  # (if needed)

# 5. Verify
curl http://new_server:8080/api/stats
```

### Scenario 2: Client Update

```cpp
// Update version in config.cpp
const std::string CLIENT_VERSION = "1.7.0";

// Update server version check
// Check /check-update endpoint

// Build new executable
// Deploy via /get-update endpoint
```

## Compliance & Auditing

### Data Integrity Verification
```bash
# Verify all files haven't been corrupted
./server-utils.sh verify-integrity Ahmad XI-A

# Result:
# ✓ file1.jpg
# ✓ file2.jpg
# ✗ file3.jpg (hash mismatch)
```

### Generate Audit Report
```bash
#!/bin/bash

# Generate compliance report
API="http://localhost:8080/api"
KEY="ADMIN-2025"

echo "WALISONGO GUARDIAN - AUDIT REPORT" > report.txt
echo "Generated: $(date)" >> report.txt
echo "" >> report.txt

# Total statistics
curl -s -H "X-API-Key: $KEY" "$API/stats" | jq '.' >> report.txt

# Daily breakdown
for date in {1..7}; do
    DATE=$(date -d "-$date days" +%Y-%m-%d)
    COUNT=$(curl -s -H "X-API-Key: $KEY" "$API/files/date/$DATE" | jq '.file_count')
    echo "$DATE: $COUNT files" >> report.txt
done
```

## Monitoring Dashboard Script

```bash
#!/bin/bash

while true; do
    clear
    echo "╔════════════════════════════════════════════╗"
    echo "║  WALISONGO GUARDIAN - LIVE MONITORING      ║"
    echo "╚════════════════════════════════════════════╝"
    echo ""
    
    # Stats
    STATS=$(curl -s -H "X-API-Key: ADMIN-2025" http://localhost:8080/api/stats)
    
    echo "📊 Statistics:"
    echo "  Total Files: $(echo $STATS | jq '.total_files')"
    echo "  Total Users: $(echo $STATS | jq '.total_users')"
    echo "  Total Size: $(echo $STATS | jq '.total_size_mb') MB"
    echo ""
    
    # Type breakdown
    echo "📁 By Type:"
    echo $STATS | jq '.type_distribution' | jq 'to_entries[] | "  \(.key): \(.value)"' -r
    echo ""
    
    # Disk usage
    echo "💾 Disk Usage:"
    du -sh uploads/ | awk '{print "  " $1}'
    echo ""
    
    # Last updated
    echo "⏱️  Last Update:"
    echo $STATS | jq '.index_last_updated' | sed 's/"//g' | awk '{print "  " $0}'
    echo ""
    
    echo "(Refreshing in 30 seconds... Press Ctrl+C to exit)"
    sleep 30
done
```

---

**Ready to integrate!** 🔗

Client and server are fully compatible and ready for deployment.
