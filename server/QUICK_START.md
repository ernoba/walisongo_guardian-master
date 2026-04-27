# Walisongo Guardian Server - Quick Start Guide

Panduan cepat untuk setup dan menjalankan server dalam 5 menit.

## Prerequisites

- Rust 1.70+ (download: https://rustup.rs/)
- 2GB RAM minimum
- 1GB free disk space

## Setup (5 Minutes)

### Step 1: Clone & Navigate
```bash
cd server
```

### Step 2: Create Config File
```bash
cp .env.example .env
```

### Step 3: Create Directories
```bash
mkdir -p uploads updates
```

### Step 4: Build
```bash
cargo build --release
```

### Step 5: Run
```bash
cargo run --release
```

**Expected output:**
```
INFO Walisongo Guardian Server v3.1 initialized successfully
INFO ✓ Listening on 0.0.0.0:8080
```

## Verify Installation

In another terminal:
```bash
# Health check
curl http://localhost:8080/health

# Expected response:
# {"status":"healthy","version":"3.1","timestamp":"..."}
```

## Quick Test

### Test Upload
```bash
# Create test file
echo "test data" > test.txt

# Upload
curl -X POST http://localhost:8080/upload \
  -H "X-API-Key: ADMIN-2025" \
  -H "X-User: TestUser[TestClass]" \
  -H "X-Type: activity" \
  -H "X-File-ID: test_001" \
  -H "X-Filename: test.txt" \
  -H "X-Chunk-Num: 0" \
  -H "X-Chunk-Total: 1" \
  --data-binary @test.txt

# Expected: HTTP 200 OK
```

### Test Query
```bash
# Get stats
curl -H "X-API-Key: ADMIN-2025" http://localhost:8080/api/stats

# Get user files
curl -H "X-API-Key: ADMIN-2025" http://localhost:8080/api/files/TestUser/TestClass
```

## Directory Structure

After first upload:
```
uploads/
├── _index.json                    ← Central file index
└── TestClass/
    └── TestUser/
        └── activity/
            └── 2024-04-28/
                └── 14-30-45/
                    ├── test_001__test.txt    ← Actual file
                    └── test_001__test.json   ← Metadata
```

## View Data

### List all files from user:
```bash
ls -R uploads/TestClass/TestUser/
```

### View file metadata:
```bash
cat uploads/TestClass/TestUser/activity/2024-04-28/14-30-45/test_001__test.json | jq '.'
```

### View statistics:
```bash
curl -H "X-API-Key: ADMIN-2025" http://localhost:8080/api/stats | jq '.'
```

## Common Tasks

### Query User Files
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/Ahmed/XI-A
```

### Query by Date
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/date/2024-04-28
```

### Query by Type
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/type/screenshot
```

### Get Metadata
```bash
curl -H "X-API-Key: ADMIN-2025" \
  http://localhost:8080/api/files/FILE_ID/metadata
```

## Utility Scripts

```bash
# Make executable
chmod +x server-utils.sh

# Health check
./server-utils.sh health-check

# Server stats
./server-utils.sh stats

# Disk usage
./server-utils.sh disk-usage

# Query user files
./server-utils.sh query-user Ahmed XI-A

# List files
./server-utils.sh list-files Ahmed XI-A

# Backup
./server-utils.sh backup ./backups

# Get help
./server-utils.sh help
```

## Stop Server

```bash
# Ctrl+C in terminal running server
# or in another terminal:
pkill walisongo-guardian-server
```

## Troubleshooting

### Port 8080 already in use
```bash
# Find what's using port 8080
lsof -i :8080

# Kill that process or use different port
# (modify SERVER_PORT in .env)
```

### Permission denied
```bash
# Make files readable
chmod -R 755 uploads/
chmod -R 755 updates/
```

### Build fails
```bash
# Update Rust
rustup update

# Clean and rebuild
cargo clean
cargo build --release
```

## Next Steps

1. **Configure for production:**
   - Change API keys in `.env`
   - Set up SSL/TLS
   - Configure firewall

2. **Connect client:**
   - Update client config with server URL
   - Verify connection
   - Monitor data flow

3. **Monitor server:**
   - Check logs: `RUST_LOG=debug cargo run`
   - Use utility scripts
   - Set up automated backups

## Documentation

- **Full API**: See [README.md](README.md)
- **Installation**: See [INSTALLATION.md](INSTALLATION.md)
- **Features**: See [FEATURES_SUMMARY.md](FEATURES_SUMMARY.md)
- **OpenAPI**: See [openapi.yaml](openapi.yaml)

## Support

- Check logs for errors
- Use utility scripts for diagnostics
- Review README.md for full documentation
- Check INSTALLATION.md for platform-specific help

---

**Ready to go!** 🚀

Your server is now running and ready to receive data from clients.
