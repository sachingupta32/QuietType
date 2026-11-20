param([ValidateSet('whisper','llama')][string]$Engine)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = (Get-ChildItem .build/toolchain -Directory | Select-Object -First 1).FullName + '\bin'
$env:Path = "$compiler;$env:Path"
$ninja = (Resolve-Path .build/ninja/ninja.exe).Path
$source = (Get-ChildItem ".build/$Engine" -Directory | Select-Object -First 1).FullName
$build = ".build/build-$Engine"
$options = @("-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
    '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -include algorithm -include cstdlib -include limits -include cstring -include numeric -include cmath -include cstdio -include ctime -include new', '-DCMAKE_CXX_STANDARD=17', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF', '-DGGML_NATIVE=OFF', '-DGGML_OPENMP=OFF',
    '-DGGML_CCACHE=OFF','-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
if ($Engine -eq 'whisper') {
    $options += @('-DWHISPER_BUILD_TESTS=OFF','-DWHISPER_BUILD_SERVER=OFF')
    $target = 'whisper-cli'
} else {
    $options += @("-DLLAMA_SOURCE=$source",'-DLLAMA_BUILD_COMMON=OFF','-DLLAMA_BUILD_EXAMPLES=OFF','-DLLAMA_BUILD_TESTS=OFF','-DLLAMA_BUILD_SERVER=OFF')
    $source = 'worker'
    $target = 'polish'
}
cmake -S $source -B $build -G Ninja @options
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --target $target -j 6
exit $LASTEXITCODE
