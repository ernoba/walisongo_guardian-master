FROM rust:1.75-slim as builder

WORKDIR /build

# Install dependencies
RUN apt-get update && apt-get install -y \
    pkg-config \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
COPY src ./src
COPY Cargo.toml .
COPY Cargo.lock .

# Build release binary
RUN cargo build --release

# ============================================================================
# Runtime Stage
# ============================================================================

FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/target/release/walisongo-guardian-server /app/server

# Create necessary directories
RUN mkdir -p /app/uploads /app/updates && \
    chmod +x /app/server

# Copy default environment
COPY .env.example /app/.env.docker

# Expose port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Run server
ENTRYPOINT ["/app/server"]
CMD []

# Labels
LABEL maintainer="Walisongo Guardian Team"
LABEL version="3.1"
LABEL description="Walisongo Guardian Server - Secure data collection platform"
