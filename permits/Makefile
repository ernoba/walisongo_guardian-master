# Konfigurasi Path dan Nama File
BINARY_NAME = permits.exe
RUST_TARGET = x86_64-pc-windows-gnu
LOCAL_TARGET_DIR = /tmp/guardian_build

# Konfigurasi Toolchain MinGW Ubuntu
CC = x86_64-w64-mingw32-gcc
AR = x86_64-w64-mingw32-ar
RANLIB = x86_64-w64-mingw32-ranlib

# Lokasi file hasil build di folder /tmp
BUILD_PATH = $(LOCAL_TARGET_DIR)/$(RUST_TARGET)/release/$(BINARY_NAME)

.PHONY: all clean build copy

# Target utama: Jalankan build lalu copy hasilnya
all: build copy

build:
	@echo "[*] Memulai Cross-Compilation di $(LOCAL_TARGET_DIR)..."
	CC_$(subst -,_,$(RUST_TARGET))=$(CC) \
	AR_$(subst -,_,$(RUST_TARGET))=$(AR) \
	RANLIB_$(subst -,_,$(RUST_TARGET))=$(RANLIB) \
	CARGO_TARGET_DIR=$(LOCAL_TARGET_DIR) \
	cargo build --release --target $(RUST_TARGET)

copy:
	@echo "[*] Memindahkan $(BINARY_NAME) kembali ke folder proyek..."
	@cp $(BUILD_PATH) .
	@echo "[SUCCESS] File siap di: $$(pwd)/$(BINARY_NAME)"

clean:
	@echo "[*] Membersihkan sisa build..."
	cargo clean
	rm -rf $(LOCAL_TARGET_DIR)
	rm -f $(BINARY_NAME)