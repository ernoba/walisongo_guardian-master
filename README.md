## step build biner code 

make
$(brew --prefix llvm)/bin/llvm-strip --strip-all WinSysHelper.exe
upx --ultra-brute --overlay=copy file.exe

## step for signature biner

openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout mykey.key -out mycert.crt
openssl pkcs12 -export -out certificate.pfx -inkey mykey.key -in mycert.crt

osslsigncode sign -pkcs12 certificate.pfx -pass "ernobatools" -in file.exe -out file_signed.exe