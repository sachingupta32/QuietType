# Fetch build-time dependencies only. Run from a Windows x64 PowerShell with CMake on PATH.
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$packages=@(
    @('toolchain','https://github.com/mstorsjo/llvm-mingw/releases/download/20261117/llvm-mingw-20261117-ucrt-x86_64.zip','4d002e330a0f703625aa5ccbc37e96f8f824413e163bfc7336774aabd2c3a412'),
    @('ninja','https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip','f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'),
    @('whisper','https://codeload.github.com/ggml-org/whisper.cpp/zip/refs/tags/v1.7.6','8c579f4468fc23ddd64092ca9d8f3933d831a358ae9b7fcc09b43d9496a020c5'),
    @('llama','https://codeload.github.com/ggml-org/llama.cpp/zip/refs/tags/b5046','628626865cdc521bcfcbd3f11dd1ca8a229e13899132847138814e88d060ebf6'),
    @('wix','https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip','6ac824e1642d6f7277d0ed7ea09411a508f6116ba6fae0aa5f2c7daa2ff43d31')
)
New-Item -Type Directory -Force .build | Out-Null
foreach($item in $packages){
    $name,$url,$expected=$item
    $zip=".build/$name.zip"
    if(!(Test-Path $zip) -or (Get-FileHash $zip).Hash -ne $expected){
        curl.exe -fLsS --retry 2 $url -o $zip
        if($LASTEXITCODE -or (Get-FileHash $zip).Hash -ne $expected){throw "Download or checksum failed: $name"}
    }
    New-Item -Type Directory -Force ".build/$name" | Out-Null
    tar -xf $zip -C ".build/$name"
    if($LASTEXITCODE){throw "Extraction failed: $name"}
}
