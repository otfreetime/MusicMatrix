param(
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Debug',
    [string]$Vst2SdkPath = 'E:/Maqam Classification/vstsdk3610_11_06_2018_build_37/VST_SDK/VST2_SDK'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build-host-x86'

Write-Host "[1/3] Configuring x86 CMake build in $buildDir"
cmake -S "$root" -B "$buildDir" -A Win32 -DMYAPP_ENABLE_VST2_HOST=ON -DMYAPP_VST2_SDK_PATH="$Vst2SdkPath"

Write-Host "[2/3] Building MyApp ($Config)"
cmake --build "$buildDir" --config $Config --target MyApp

$hostExePath = Join-Path $buildDir "MyApp_artefacts/$Config/MyApp.exe"
if (!(Test-Path $hostExePath)) {
    throw "x86 host not found at $hostExePath"
}

Write-Host "[3/3] Done. x86 host is ready: $hostExePath"
Write-Host "Run it with:"
Write-Host "  & '$hostExePath'"
