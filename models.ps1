param([string]$ModelDirectory = (Join-Path $env:LOCALAPPDATA 'QuietType\models'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $ModelDirectory | Out-Null
$models = @(
    @('ggml-base.en-q5_1.bin', 'ggerganov/whisper.cpp', '4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f'),
    @('qwen2.5-1.5b-instruct-q4_k_m.gguf', 'Qwen/Qwen2.5-1.5B-Instruct-GGUF', '6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e')
)
$lock = [IO.File]::Open((Join-Path $ModelDirectory 'setup.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    foreach ($model in $models) {
        $name, $repo, $digest = $model
        $target = Join-Path $ModelDirectory $name
        if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -eq $digest) { continue }
        $partial = "$target.part"
        Write-Host "Downloading $name"
        $url = "https://huggingface.co/$repo/resolve/main/$name"
        $size = if ($name.EndsWith('.bin')) { 59721011 } else { 1117320736 }
        $received = if (Test-Path -LiteralPath $partial) { (Get-Item -LiteralPath $partial).Length } else { 0 }
        if ($received -gt $size) { Remove-Item -LiteralPath $partial; $received = 0 }
        if ($received -lt $size) {
            $curl = "$env:SystemRoot\System32\curl.exe"
            & $curl -fL --retry 3 --connect-timeout 30 --speed-limit 1024 --speed-time 60 -C - -o $partial $url
            if ($LASTEXITCODE -eq 33) {
                # A mirror without byte-range support requires a fresh transfer.
                & $curl -fL --retry 3 --connect-timeout 30 --speed-limit 1024 --speed-time 60 -o $partial $url
            }
            if ($LASTEXITCODE -ne 0) { throw "Download interrupted. Choose Set up models again to resume." }
        }
        if ((Get-FileHash -LiteralPath $partial).Hash -ne $digest) {
            Remove-Item -LiteralPath $partial
            throw "Verification failed for $name. Retry setup."
        }
        Move-Item -LiteralPath $partial -Destination $target -Force
    }
    [IO.File]::WriteAllText((Join-Path $ModelDirectory 'verified-v1'), 'ok')
} finally { $lock.Dispose() }
