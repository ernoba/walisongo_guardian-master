#!/bin/bash

# Walisongo Guardian Server - Utility Scripts
# Usage: ./server-utils.sh [command] [options]

set -e

# ============================================================================
# CONFIGURATION
# ============================================================================

SERVER_URL="${SERVER_URL:-http://localhost:8080}"
API_KEY="${API_KEY:-ADMIN-2025}"
UPLOADS_DIR="${UPLOADS_DIR:-./uploads}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}ℹ $1${NC}"
}

# ============================================================================
# HEALTH CHECK COMMANDS
# ============================================================================

cmd_health_check() {
    print_header "Server Health Check"
    
    local response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/health")
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | sed '$d')
    
    if [ "$http_code" -eq 200 ]; then
        print_success "Server is healthy"
        echo "$body" | jq '.'
    else
        print_error "Server health check failed (HTTP $http_code)"
    fi
}

# ============================================================================
# STATISTICS & MONITORING
# ============================================================================

cmd_stats() {
    print_header "Server Statistics"
    
    local response=$(curl -s -H "X-API-Key: $API_KEY" \
        "$SERVER_URL/api/stats")
    
    echo "$response" | jq '.'
}

cmd_disk_usage() {
    print_header "Disk Usage Analysis"
    
    if [ ! -d "$UPLOADS_DIR" ]; then
        print_error "Uploads directory not found: $UPLOADS_DIR"
        return 1
    fi
    
    print_info "Total size:"
    du -sh "$UPLOADS_DIR"
    
    echo ""
    print_info "Size by class:"
    du -sh "$UPLOADS_DIR"/*/ 2>/dev/null || print_error "No classes found"
    
    echo ""
    print_info "Size by data type (Top 10):"
    find "$UPLOADS_DIR" -maxdepth 3 -type d -name "screenshot" -o -type d -name "keylog" -o -type d -name "activity" | \
        xargs du -sh 2>/dev/null | sort -rh | head -10
}

# ============================================================================
# QUERY COMMANDS
# ============================================================================

cmd_query_user() {
    local user=$1
    local class=$2
    
    if [ -z "$user" ] || [ -z "$class" ]; then
        print_error "Usage: query-user <name> <class>"
        echo "Example: query-user Ahmad XI-A"
        return 1
    fi
    
    print_header "Files for $user [$class]"
    
    local response=$(curl -s -H "X-API-Key: $API_KEY" \
        "$SERVER_URL/api/files/$user/$class")
    
    echo "$response" | jq '.'
}

cmd_query_type() {
    local type=$1
    
    if [ -z "$type" ]; then
        print_error "Usage: query-type <type>"
        echo "Available types: screenshot, keylog, activity, video, audio"
        return 1
    fi
    
    print_header "Files of type: $type"
    
    local response=$(curl -s -H "X-API-Key: $API_KEY" \
        "$SERVER_URL/api/files/type/$type")
    
    echo "$response" | jq '.'
}

cmd_query_date() {
    local date=$1
    
    if [ -z "$date" ]; then
        date=$(date +%Y-%m-%d)
        print_info "No date provided, using today: $date"
    fi
    
    print_header "Files from $date"
    
    local response=$(curl -s -H "X-API-Key: $API_KEY" \
        "$SERVER_URL/api/files/date/$date")
    
    echo "$response" | jq '.'
}

cmd_get_metadata() {
    local file_id=$1
    
    if [ -z "$file_id" ]; then
        print_error "Usage: get-metadata <file_id>"
        return 1
    fi
    
    print_header "Metadata for $file_id"
    
    local response=$(curl -s -H "X-API-Key: $API_KEY" \
        "$SERVER_URL/api/files/$file_id/metadata")
    
    echo "$response" | jq '.'
}

# ============================================================================
# DATA MANAGEMENT
# ============================================================================

cmd_export_data() {
    local user=$1
    local class=$2
    local output_dir=${3:-.}
    
    if [ -z "$user" ] || [ -z "$class" ]; then
        print_error "Usage: export-data <name> <class> [output_dir]"
        return 1
    fi
    
    print_header "Exporting data for $user [$class]"
    
    local source_dir="$UPLOADS_DIR/$class/$user"
    
    if [ ! -d "$source_dir" ]; then
        print_error "User directory not found: $source_dir"
        return 1
    fi
    
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local export_file="$output_dir/${class}_${user}_export_${timestamp}.tar.gz"
    
    print_info "Creating archive: $export_file"
    tar -czf "$export_file" -C "$UPLOADS_DIR" "$class/$user"
    
    local size=$(du -h "$export_file" | cut -f1)
    print_success "Export completed: $export_file ($size)"
}

cmd_list_files() {
    local user=$1
    local class=$2
    
    if [ -z "$user" ] || [ -z "$class" ]; then
        print_error "Usage: list-files <name> <class>"
        return 1
    fi
    
    print_header "Files for $user [$class]"
    
    local user_dir="$UPLOADS_DIR/$class/$user"
    
    if [ ! -d "$user_dir" ]; then
        print_error "User directory not found: $user_dir"
        return 1
    fi
    
    find "$user_dir" -type f -not -name "*.json" -not -name "_*" | \
        while read file; do
            local size=$(du -h "$file" | cut -f1)
            local modified=$(stat -f "%Sm" -t "%Y-%m-%d %H:%M:%S" "$file" 2>/dev/null || stat -c "%y" "$file" 2>/dev/null | cut -d. -f1)
            printf "%-40s %10s %s\n" "$(basename "$file")" "$size" "$modified"
        done | sort -k3
}

cmd_archive_old_data() {
    local days=${1:-30}
    
    print_header "Archiving data older than $days days"
    
    if [ ! -d "$UPLOADS_DIR" ]; then
        print_error "Uploads directory not found: $UPLOADS_DIR"
        return 1
    fi
    
    print_info "Finding files older than $days days..."
    
    local count=$(find "$UPLOADS_DIR" -type f -not -name "*.json" -not -name "_*" -mtime +$days | wc -l)
    print_info "Found $count files to archive"
    
    if [ "$count" -gt 0 ]; then
        local timestamp=$(date +%Y%m%d_%H%M%S)
        local archive_file="./archived_${timestamp}.tar.gz"
        
        find "$UPLOADS_DIR" -type f -not -name "*.json" -not -name "_*" -mtime +$days | \
            tar -czf "$archive_file" -T -
        
        local size=$(du -h "$archive_file" | cut -f1)
        print_success "Archive created: $archive_file ($size)"
    else
        print_info "No files to archive"
    fi
}

# ============================================================================
# VERIFICATION & INTEGRITY CHECK
# ============================================================================

cmd_verify_integrity() {
    local user=$1
    local class=$2
    
    if [ -z "$user" ] || [ -z "$class" ]; then
        print_error "Usage: verify-integrity <name> <class>"
        return 1
    fi
    
    print_header "Verifying file integrity for $user [$class]"
    
    local user_dir="$UPLOADS_DIR/$class/$user"
    
    if [ ! -d "$user_dir" ]; then
        print_error "User directory not found: $user_dir"
        return 1
    fi
    
    local errors=0
    
    find "$user_dir" -name "*.json" -type f | while read metadata_file; do
        local data_file="${metadata_file%.json}"
        
        if [ ! -f "$data_file" ]; then
            print_error "Missing data file: $data_file"
            ((errors++))
            continue
        fi
        
        local stored_hash=$(jq -r '.file_hash' "$metadata_file")
        local actual_hash=$(sha256sum "$data_file" | cut -d' ' -f1)
        
        if [ "$stored_hash" = "$actual_hash" ]; then
            print_success "$(basename "$data_file")"
        else
            print_error "Hash mismatch: $(basename "$data_file")"
            ((errors++))
        fi
    done
    
    if [ "$errors" -eq 0 ]; then
        print_success "All files verified successfully"
    else
        print_error "Found $errors integrity issues"
    fi
}

# ============================================================================
# BACKUP & RESTORE
# ============================================================================

cmd_backup() {
    local backup_dir=${1:-.}
    
    print_header "Creating server backup"
    
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local backup_file="$backup_dir/walisongo_backup_${timestamp}.tar.gz"
    
    if [ ! -d "$backup_dir" ]; then
        mkdir -p "$backup_dir"
    fi
    
    print_info "Backing up uploads, updates, and index..."
    tar -czf "$backup_file" \
        -C "$(dirname "$UPLOADS_DIR")" "$(basename "$UPLOADS_DIR")" \
        -C "$(dirname ./updates)" "updates" \
        2>/dev/null || true
    
    local size=$(du -h "$backup_file" | cut -f1)
    print_success "Backup created: $backup_file ($size)"
}

# ============================================================================
# CLEANUP
# ============================================================================

cmd_cleanup_temp() {
    print_header "Cleaning up temporary files"
    
    if [ ! -d "$UPLOADS_DIR" ]; then
        print_error "Uploads directory not found: $UPLOADS_DIR"
        return 1
    fi
    
    local temp_files=$(find "$UPLOADS_DIR" -name "*.dat" -type f | wc -l)
    
    if [ "$temp_files" -gt 0 ]; then
        print_info "Found $temp_files temporary files"
        find "$UPLOADS_DIR" -name "*.dat" -type f -delete
        print_success "Cleaned up $temp_files temporary files"
    else
        print_info "No temporary files found"
    fi
}

# ============================================================================
# HELP COMMAND
# ============================================================================

cmd_help() {
    cat << 'EOF'
Walisongo Guardian Server - Utility Scripts

USAGE:
    ./server-utils.sh [command] [options]

COMMANDS:

Health & Monitoring:
    health-check              - Check server health status
    stats                     - Display server statistics
    disk-usage                - Show disk usage analysis

Query Commands:
    query-user <name> <class> - Query files for specific user
    query-type <type>         - Query all files of specific type
    query-date [date]         - Query files from specific date (YYYY-MM-DD)
    get-metadata <file_id>    - Get detailed metadata for file
    list-files <name> <class> - List all files for user (local)

Data Management:
    export-data <name> <class> [dir] - Export user data as tar.gz
    archive-old-data [days]          - Archive files older than N days
    verify-integrity <name> <class>  - Verify file integrity with hashes
    backup [dir]                     - Create full server backup

Maintenance:
    cleanup-temp              - Remove temporary .dat files

Information:
    help                      - Show this help message

ENVIRONMENT VARIABLES:
    SERVER_URL   - Server base URL (default: http://localhost:8080)
    API_KEY      - API authentication key (default: ADMIN-2025)
    UPLOADS_DIR  - Upload root directory (default: ./uploads)

EXAMPLES:
    # Check server health
    ./server-utils.sh health-check

    # Query files for Ahmad in class XI-A
    ./server-utils.sh query-user Ahmad XI-A

    # Query all screenshots
    ./server-utils.sh query-type screenshot

    # Export user data
    ./server-utils.sh export-data Ahmad XI-A ./exports

    # Verify data integrity
    ./server-utils.sh verify-integrity Ahmad XI-A

    # Create backup
    ./server-utils.sh backup ./backups

    # Archive data older than 60 days
    ./server-utils.sh archive-old-data 60

EOF
}

# ============================================================================
# MAIN COMMAND DISPATCHER
# ============================================================================

main() {
    local command=$1
    shift || true
    
    case "$command" in
        health-check)
            cmd_health_check
            ;;
        stats)
            cmd_stats
            ;;
        disk-usage)
            cmd_disk_usage
            ;;
        query-user)
            cmd_query_user "$@"
            ;;
        query-type)
            cmd_query_type "$@"
            ;;
        query-date)
            cmd_query_date "$@"
            ;;
        get-metadata)
            cmd_get_metadata "$@"
            ;;
        list-files)
            cmd_list_files "$@"
            ;;
        export-data)
            cmd_export_data "$@"
            ;;
        archive-old-data)
            cmd_archive_old_data "$@"
            ;;
        verify-integrity)
            cmd_verify_integrity "$@"
            ;;
        backup)
            cmd_backup "$@"
            ;;
        cleanup-temp)
            cmd_cleanup_temp
            ;;
        help|--help|-h)
            cmd_help
            ;;
        *)
            if [ -z "$command" ]; then
                cmd_help
            else
                print_error "Unknown command: $command"
                echo "Run './server-utils.sh help' for usage information"
                exit 1
            fi
            ;;
    esac
}

main "$@"
