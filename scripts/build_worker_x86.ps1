param(
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Debug',
    [string]$Vst2SdkPath = 'E:/Maqam Classification/vstsdk3610_11_06_2018_build_37/VST_SDK/VST2_SDK'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build-x86'

Write-Host "[1/3] Configuring x86 CMake build in $buildDir"
cmake -S "$root" -B "$buildDir" -A Win32 -DMYAPP_ENABLE_VST2_HOST=ON -DMYAPP_VST2_SDK_PATH="$Vst2SdkPath"

Write-Host "[2/3] Building MyAppBridgeWorker ($Config)"
cmake --build "$buildDir" --config $Config --target MyAppBridgeWorker

$worker = Join-Path $buildDir "MyAppBridgeWorker_artefacts/$Config/MyAppBridgeWorker.exe"
if (!(Test-Path $worker)) {
    throw "x86 worker not found at $worker"
}

Write-Host "[3/3] Done. x86 worker is ready: $worker"
Write-Host "Optional: set env var if you want explicit override:"
Write-Host "  `$env:MYAPP_BRIDGE_WORKER_X86_PATH = '$worker'"
