$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot)
$compiler=(Get-ChildItem .build/toolchain -Directory | Select-Object -First 1).FullName
$env:Path="$compiler\bin;$env:Path"
foreach($test in 'desktop-probe','controls','field-switch'){
    clang++ -std=c++17 -O2 -municode "tests/$test.cpp" -o ".build/$test.exe" -lwinmm -lcomctl32 -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -lgdi32 -lpsapi
    if($LASTEXITCODE){throw "Could not compile $test"}
}
Copy-Item dist/app/*.dll .build -Force
