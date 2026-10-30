#!/usr/bin/env bash
# Сборка BINDER на Linux / WSL / macOS.
# Нужны: clang, lld (lld-link), llvm-dlltool, llvm-rc  (пакеты clang + lld + llvm).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=out
mkdir -p "$OUT"

for d in build/defs/*.def; do
  n=$(basename "$d" .def)
  llvm-dlltool -m i386 -k -d "$d" -l "$OUT/$n.lib"
done

(cd res && llvm-rc /fo "../$OUT/rtbind.res" rtbind.rc)

clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -mno-sse \
  -Wall -Wno-unused-function -Wno-missing-braces -c src/rtbind.c -o "$OUT/rtbind.o"

lld-link /nologo /entry:Entry /subsystem:windows,5.01 /nodefaultlib /machine:x86 \
  /largeaddressaware /dynamicbase /nxcompat /safeseh:no /opt:ref \
  /out:"$OUT/RTBind.exe" "$OUT/rtbind.o" "$OUT/rtbind.res" \
  "$OUT"/kernel32.lib "$OUT"/user32.lib "$OUT"/gdi32.lib "$OUT"/msimg32.lib "$OUT"/shell32.lib "$OUT"/comctl32.lib

echo "Готово: $OUT/RTBind.exe"
