# Pengaturan Compiler untuk Cross-Compile ke Windows
CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -O3 -static -mwindows -I. -DUNICODE -D_UNICODE

# Library Windows yang harus di-link secara manual
LIBS = -lwininet -lwinhttp -lws2_32 -lwlanapi -lgdi32 -lgdiplus -ladvapi32 -lshell32 -lrpcrt4 -lole32 -loleaut32 -luuid -ltaskschd -lstdc++fs

# Daftar File Sumber (Otomatis mengambil semua .cpp di folder core)
SRCS = main.cpp $(wildcard core/*.cpp)

# Nama Output sesuai Config::EXE_NAME
TARGET = WinSysHelper.exe

# Aturan Utama
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

# Aturan untuk membersihkan file hasil build
clean:
	rm -f $(TARGET)