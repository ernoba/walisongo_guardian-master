# Walisongo Guardian Server - Installation & Deployment Guide

## Table of Contents
1. [Local Development](#local-development)
2. [Docker Deployment](#docker-deployment)
3. [Linux Systemd Deployment](#linux-systemd-deployment)
4. [Windows Deployment](#windows-deployment)
5. [Production Setup](#production-setup)
6. [Post-Installation](#post-installation)

---

## Local Development

### Requirements
- Rust 1.70+ with Cargo
- 2GB RAM
- 1GB free disk space
- Linux/macOS/Windows

### Installation Steps

1. **Clone repository and navigate to server directory:**
```bash
cd server
```

2. **Install Rust (if not already installed):**
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

3. **Copy environment configuration:**
```bash
cp .env.example .env
```

4. **Create required directories:**
```bash
mkdir -p uploads updates
```

5. **Build project:**
```bash
cargo build --release
```

6. **Run server:**
```bash
cargo run --release
```

Server akan start di `http://0.0.0.0:8080`

### Verify Installation

```bash
# Check health
curl http://localhost:8080/health

# Expected output:
# {"status":"healthy","version":"3.1","timestamp":"..."}
```

---

## Docker Deployment

### Requirements
- Docker 20.10+
- Docker Compose 2.0+
- 2GB available memory
- 5GB disk space

### Quick Start

1. **Navigate to server directory:**
```bash
cd server
```

2. **Build Docker image:**
```bash
docker build -t walisongo-guardian-server:3.1 .
```

3. **Run with Docker Compose:**
```bash
docker-compose up -d
```

4. **Verify container:**
```bash
docker ps | grep walisongo
docker logs walisongo-guardian-server
```

### Docker Commands

**Start server:**
```bash
docker-compose up -d
```

**Stop server:**
```bash
docker-compose down
```

**View logs:**
```bash
docker-compose logs -f
```

**Access shell:**
```bash
docker exec -it walisongo-guardian-server bash
```

**Check health:**
```bash
docker exec walisongo-guardian-server curl localhost:8080/health
```

### Docker Networking

Server dapat diakses dari:
- **Internal**: `http://walisongo-server:8080` (dari container lain)
- **Host**: `http://localhost:8080`
- **Remote**: `http://<server-ip>:8080` (jika port 8080 terbuka)

### Persistent Volumes

Data disimpan di Docker volumes:
- `walisongo_uploads` - Semua uploaded files
- `walisongo_updates` - Update binaries

Untuk backup:
```bash
docker run --rm -v walisongo_uploads:/data -v $(pwd):/backup \
  alpine tar czf /backup/uploads_backup.tar.gz -C /data .
```

---

## Linux Systemd Deployment

### Requirements
- Linux (Ubuntu 20.04+, Debian 11+, CentOS 8+)
- Rust or pre-built binary
- 2GB RAM
- 5GB disk space

### Installation Steps

1. **Build release binary:**
```bash
cargo build --release
```

2. **Create user dan directories:**
```bash
sudo useradd -r -s /bin/bash walisongo
sudo mkdir -p /opt/walisongo-server /var/lib/walisongo/{uploads,updates}
sudo chown -R walisongo:walisongo /opt/walisongo-server /var/lib/walisongo
```

3. **Copy binary:**
```bash
sudo cp target/release/walisongo-guardian-server /opt/walisongo-server/
sudo chmod +x /opt/walisongo-server/walisongo-guardian-server
```

4. **Install systemd service:**
```bash
sudo cp walisongo-guardian.service /etc/systemd/system/
sudo systemctl daemon-reload
```

5. **Edit service configuration (optional):**
```bash
sudo nano /etc/systemd/system/walisongo-guardian.service
```

6. **Enable and start service:**
```bash
sudo systemctl enable walisongo-guardian.service
sudo systemctl start walisongo-guardian.service
```

### Service Management

**Start server:**
```bash
sudo systemctl start walisongo-guardian
```

**Stop server:**
```bash
sudo systemctl stop walisongo-guardian
```

**Restart server:**
```bash
sudo systemctl restart walisongo-guardian
```

**Check status:**
```bash
sudo systemctl status walisongo-guardian
```

**View logs:**
```bash
sudo journalctl -u walisongo-guardian -f
```

**Enable auto-start on boot:**
```bash
sudo systemctl enable walisongo-guardian
```

### Monitoring

**Check running process:**
```bash
ps aux | grep walisongo-guardian
```

**Monitor resource usage:**
```bash
top -p $(pgrep -f walisongo-guardian)
```

**Check disk usage:**
```bash
du -sh /var/lib/walisongo/uploads
```

---

## Windows Deployment

### Requirements
- Windows 10 Pro / Windows Server 2016+
- Rust or pre-built binary
- 2GB RAM
- 5GB disk space

### Installation Steps

1. **Download or build binary:**
```powershell
# Option A: Build from source
cargo build --release
# Binary akan di: target\release\walisongo-guardian-server.exe

# Option B: Download pre-built binary
# Download dari GitHub releases
```

2. **Create installation directory:**
```powershell
New-Item -ItemType Directory -Path "C:\Walisongo\Server" -Force
New-Item -ItemType Directory -Path "C:\Walisongo\uploads" -Force
New-Item -ItemType Directory -Path "C:\Walisongo\updates" -Force
```

3. **Copy executable:**
```powershell
Copy-Item target\release\walisongo-guardian-server.exe C:\Walisongo\Server\
```

4. **Create batch script untuk startup:**
```powershell
@"
@echo off
cd /d "C:\Walisongo\Server"
set UPLOAD_ROOT_PATH=C:\Walisongo\uploads
set UPDATE_ROOT_PATH=C:\Walisongo\updates
set RUST_LOG=info
walisongo-guardian-server.exe
pause
"@ | Out-File -Encoding ASCII C:\Walisongo\Server\run.bat
```

5. **Create Windows Service (optional):**
```powershell
# Install NSSM (Non-Sucking Service Manager)
# https://nssm.cc/download

# Install service
nssm install WalisongoGuardian "C:\Walisongo\Server\walisongo-guardian-server.exe"
nssm start WalisongoGuardian
```

### Run Server

**Manual run:**
```powershell
C:\Walisongo\Server\walisongo-guardian-server.exe
```

**As Windows Service:**
```powershell
# Start
net start WalisongoGuardian

# Stop
net stop WalisongoGuardian

# Status
sc query WalisongoGuardian
```

---

## Production Setup

### Pre-production Checklist

- [ ] Change API keys in `.env`
- [ ] Change encryption key
- [ ] Set up firewall rules
- [ ] Configure SSL/TLS (reverse proxy)
- [ ] Set up backups
- [ ] Configure monitoring/logging
- [ ] Test client connection
- [ ] Load testing
- [ ] Security audit

### Security Configuration

#### 1. Change API Keys

Edit `.env`:
```bash
# Generate strong keys (32+ characters)
STATIC_ENCRYPTION_KEY=your-secure-32-char-key-here
API_AUTH_KEY=your-secure-32-char-auth-key
```

#### 2. Set Up SSL/TLS with Nginx

```bash
# Install Nginx
sudo apt install nginx

# Configure reverse proxy
sudo nano /etc/nginx/sites-available/default
```

```nginx
upstream walisongo_server {
    server 127.0.0.1:8080;
    keepalive 32;
}

server {
    listen 443 ssl http2;
    server_name your-domain.com;

    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;

    location / {
        proxy_pass http://walisongo_server;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 600s;
        proxy_send_timeout 600s;
    }
}

# Redirect HTTP to HTTPS
server {
    listen 80;
    server_name your-domain.com;
    return 301 https://$server_name$request_uri;
}
```

#### 3. Set Up Firewall

```bash
# UFW (Ubuntu)
sudo ufw allow 22/tcp      # SSH
sudo ufw allow 80/tcp      # HTTP
sudo ufw allow 443/tcp     # HTTPS
sudo ufw enable
```

#### 4. Configure Automatic Backups

```bash
# Create backup script
cat > /opt/backup.sh << 'EOF'
#!/bin/bash
BACKUP_DIR="/mnt/backups"
DATE=$(date +%Y%m%d_%H%M%S)

mkdir -p $BACKUP_DIR
tar -czf $BACKUP_DIR/walisongo_${DATE}.tar.gz /var/lib/walisongo/

# Keep only last 30 backups
find $BACKUP_DIR -type f -mtime +30 -delete
EOF

chmod +x /opt/backup.sh

# Add to crontab (daily backup at 2 AM)
(crontab -l 2>/dev/null; echo "0 2 * * * /opt/backup.sh") | crontab -
```

#### 5. Set Up Monitoring

```bash
# Monitor disk space
watch -n 60 'du -sh /var/lib/walisongo/uploads'

# Monitor server health
curl -H "X-API-Key: ADMIN-2025" \
  https://your-domain.com/api/stats | jq '.total_size_mb'
```

### Performance Optimization

#### 1. Increase File Descriptors

```bash
# Edit /etc/security/limits.conf
echo "walisongo soft nofile 65536" | sudo tee -a /etc/security/limits.conf
echo "walisongo hard nofile 65536" | sudo tee -a /etc/security/limits.conf
```

#### 2. Kernel Tuning

```bash
# Edit /etc/sysctl.conf
echo "net.core.somaxconn = 65536" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.tcp_max_syn_backlog = 65536" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

#### 3. Nginx Tuning

```nginx
worker_processes auto;
worker_connections 65536;

client_max_body_size 100M;
proxy_cache_bypass $http_pragma $http_authorization;
```

---

## Post-Installation

### 1. Verify Installation

```bash
# Health check
curl http://localhost:8080/health

# Check stats
curl -H "X-API-Key: ADMIN-2025" http://localhost:8080/api/stats

# Test upload
curl -X POST http://localhost:8080/upload \
  -H "X-API-Key: ADMIN-2025" \
  -H "X-User: TestUser[TestClass]" \
  -H "X-Type: test" \
  -H "X-File-ID: test_001" \
  -H "X-Filename: test.txt" \
  -H "X-Chunk-Num: 0" \
  -H "X-Chunk-Total: 1" \
  --data-binary @test.txt
```

### 2. Test Client Connection

Dari client machine:
```cpp
// Update DYNAMIC_SERVER_URL di config
Config::DYNAMIC_SERVER_URL = "http://your-server:8080";
```

### 3. Set Up Monitoring Scripts

```bash
# Make utility script executable
chmod +x server-utils.sh

# Test utility
./server-utils.sh health-check
./server-utils.sh stats
```

### 4. Regular Maintenance

**Daily:**
- Check disk space
- Monitor logs for errors
- Verify active clients

**Weekly:**
- Run integrity check
- Archive old data
- Review statistics

**Monthly:**
- Full system backup
- Security audit
- Performance analysis

---

## Troubleshooting

### Server Won't Start

```bash
# Check logs
journalctl -u walisongo-guardian -n 50

# Check port availability
netstat -tuln | grep 8080

# Check permissions
ls -la /var/lib/walisongo/
```

### High Disk Usage

```bash
# Find largest files
find /var/lib/walisongo/uploads -type f -size +100M

# Archive old data
./server-utils.sh archive-old-data 30

# Check index size
du -h /var/lib/walisongo/uploads/_index.json
```

### Connection Issues

```bash
# Check firewall
sudo ufw status
sudo iptables -L

# Test connectivity
curl -v http://localhost:8080/health

# Check DNS (if using domain)
nslookup your-domain.com
```

---

## Support & Maintenance

- Check logs: `journalctl -u walisongo-guardian -f`
- Use utility scripts: `./server-utils.sh help`
- Backup regularly: `./server-utils.sh backup`
- Monitor stats: `./server-utils.sh stats`

---

## Version Upgrade

```bash
# Stop server
sudo systemctl stop walisongo-guardian

# Backup data
./server-utils.sh backup

# Build new version
cargo build --release

# Replace binary
sudo cp target/release/walisongo-guardian-server /opt/walisongo-server/

# Start server
sudo systemctl start walisongo-guardian

# Verify upgrade
curl http://localhost:8080/health
```
