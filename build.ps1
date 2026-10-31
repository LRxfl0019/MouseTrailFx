# MouseTrailFx — 构建脚本
# 依赖：MinGW-w64 (g++ / windres)
# 用法：powershell -ExecutionPolicy Bypass -File build.ps1
param([string]$Out = "dist\MouseTrailFx.exe")

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path dist | Out-Null
    windres app.rc -O coff -o build_res.o
    g++ -std=c++20 -O2 -s -municode -mwindows -static src/main.cpp build_res.o `
        -o $Out -lgdi32 -luser32 -lwinmm -lshell32 -lole32 -luuid
    Remove-Item build_res.o -ErrorAction SilentlyContinue
    Write-Host "built: $Out"
} finally {
    Pop-Location
}
