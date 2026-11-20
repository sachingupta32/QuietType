$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = (Get-ChildItem .build/toolchain -Directory | Select-Object -First 1).FullName
$env:Path = "$compiler\bin;$env:Path"
$output = New-Item -ItemType Directory -Force dist/app
Push-Location src
try { windres -i app.rc -o ../.build/app.res.o
    if ($LASTEXITCODE) { throw 'Resource compile failed' }
} finally { Pop-Location }
clang++ -std=c++17 -O2 -mwindows -municode src/main.cpp .build/app.res.o -o "$output/QuietType.exe" -lwinmm -lcomctl32 -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -lgdi32
if ($LASTEXITCODE) { throw 'App compile failed' }
Copy-Item .build/build-whisper/bin/whisper-cli.exe,.build/build-llama/polish.exe,models.ps1 $output -Force
foreach ($dll in 'libc++.dll','libunwind.dll','libwinpthread-1.dll') {
    Copy-Item "$compiler/x86_64-w64-mingw32/bin/$dll" $output -Force
}
Copy-Item .build/whisper/whisper.cpp-1.7.6/LICENSE "$output/LICENSE-whisper.txt"
Copy-Item .build/llama/llama.cpp-b5046/LICENSE "$output/LICENSE-llama.txt"
