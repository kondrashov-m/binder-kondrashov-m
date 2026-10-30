@echo off
rem Сборка BINDER в Windows.
rem Нужен LLVM для Windows (clang, lld-link, llvm-dlltool, llvm-rc в PATH):
rem   winget install LLVM.LLVM    или    https://github.com/llvm/llvm-project/releases
chcp 65001 >nul
setlocal
cd /d "%~dp0.."
if not exist out mkdir out

for %%f in (build\defs\*.def) do (
  llvm-dlltool -m i386 -k -d "%%f" -l "out\%%~nf.lib" || goto :fail
)

pushd res
llvm-rc /fo ..\out\rtbind.res rtbind.rc || (popd & goto :fail)
popd

clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -mno-sse -Wall -Wno-unused-function -Wno-missing-braces -c src\rtbind.c -o out\rtbind.o || goto :fail

lld-link /nologo /entry:Entry /subsystem:windows,5.01 /nodefaultlib /machine:x86 /largeaddressaware /dynamicbase /nxcompat /safeseh:no /opt:ref /out:out\RTBind.exe out\rtbind.o out\rtbind.res out\kernel32.lib out\user32.lib out\gdi32.lib out\msimg32.lib out\shell32.lib out\comctl32.lib || goto :fail

echo.
echo Готово: out\RTBind.exe
exit /b 0

:fail
echo Ошибка сборки
exit /b 1
