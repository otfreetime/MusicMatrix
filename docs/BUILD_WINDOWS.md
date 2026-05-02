# Windows Build Instructions

## Prerequisites Check

```powershell
# Check CMake
cmake --version
# Expected: cmake version 3.21+

# Check Git
git --version
# Expected: git version 2.x+

# Check Visual Studio (if using VS generator)
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin"
# Expected: VS 2026/2025 MSBuild path exists
```

If any are missing, install from:
- **CMake:** https://cmake.org/download/
- **Git:** https://git-scm.com/download/win
- **Visual Studio:** https://visualstudio.microsoft.com/downloads/

---

## Build Steps (PowerShell)

### Step 1: Navigate to Project

```powershell
cd "e:\Maqam Classification\MyApp"
```

### Step 2: Create Build Directory

```powershell
if (Test-Path build) { Remove-Item -Recurse -Force build }
mkdir build
cd build
```

### Step 3: Configure with CMake

**Option A: Visual Studio 2022 (Recommended)**

```powershell
cmake -G "Visual Studio 17 2022" -A x64 ..
```

**Option B: Ninja (Faster)**

```powershell
# (Requires Ninja installed: choco install ninja or download from GitHub)
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
```

**Option C: Ninja Multi-Config**

```powershell
cmake -G "Ninja Multi-Config" ..
```

### Step 4: Build

**Using Visual Studio Generator:**

```powershell
# Debug build
cmake --build . --config Debug --parallel 4

# Release build (optimized)
cmake --build . --config Release --parallel 4
```

**Using Ninja:**

```powershell
# Debug
cmake --build . --config Debug

# Release
cmake --build . --config Release
```

### Step 5: Run Tests (Optional)

```powershell
# Run all tests
ctest --verbose --output-on-failure

# Run specific test
ctest -R "KnownPluginList" --verbose
```

### Step 6: Run Application

```powershell
# Debug executable
.\Debug\MyApp.exe

# Or Release
.\Release\MyApp.exe
```

---

## Full Walkthrough (Copy & Paste)

```powershell
# 1. Navigate
cd "e:\Maqam Classification\MyApp"

# 2. Clean (if rebuilding)
if (Test-Path build) { Remove-Item -Recurse -Force build }

# 3. Create & enter build directory
mkdir build -ErrorAction SilentlyContinue
cd build

# 4. Configure
cmake -G "Visual Studio 17 2022" -A x64 .. 2>&1 | Tee-Object -FilePath cmake_config.log

# 5. Build (Debug)
cmake --build . --config Debug --parallel 4 2>&1 | Tee-Object -FilePath cmake_build.log

# 6. Run tests
ctest --verbose --output-on-failure

# 7. Launch app
.\Debug\MyApp.exe
```

---

## Troubleshooting

### CMake Error: "Visual Studio 17 not found"

**Check installed VS versions:**

```powershell
ls "C:\Program Files\Microsoft Visual Studio\18"
```

**Use correct version:**

```powershell
# If only Community edition:
cmake -G "Visual Studio 18 2026" -A x64 ..

# If using Professional/Enterprise:
# (Same command; VS auto-detects)
```

### CMake Error: "Git not found"

**FetchContent requires Git to download JUCE:**

```powershell
# Install Git from https://git-scm.com/download/win
# Or via Chocolatey:
choco install git
```

### Build Error: "JUCE download timeout"

**Retry with longer timeout:**

```powershell
cmake --build . --config Debug --parallel 1
```

**Or manually pre-download JUCE:**

```powershell
cd build/_deps

# Clone JUCE manually
git clone https://github.com/juce-framework/JUCE.git juce-src
cd juce-src
git checkout 8.0.12
cd ..\..\..
```

### Build Error: "juce_audio_processors not found"

**Verify CMakeLists.txt linking:**

```powershell
# Open CMakeLists.txt and ensure:
# target_link_libraries(MyApp PRIVATE
#     juce::juce_audio_processors
#     ...
# )

# Then rebuild:
cmake --build . --config Debug --clean-first
```

### Test Error: "PluginHostingTests not found"

**Ensure Catch2 downloaded:**

```powershell
# Check if Catch2 exists:
ls build/_deps/catch2-src

# If missing, reconfigure:
rm -Recurse -Force build/_deps/catch2-*
cmake --build . --config Debug
```

---

## Build Customization

### Use Ninja Instead of Visual Studio

```powershell
# Install Ninja (if not already):
choco install ninja

# Or download from:
# https://github.com/ninja-build/ninja/releases

# Then configure:
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Build Only (Skip Tests)

```powershell
cmake --build . --config Debug --target MyApp
.\Debug\MyApp.exe
```

### Build Only Tests

```powershell
cmake --build . --config Debug --target PluginHostingTests
.\Debug\PluginHostingTests.exe
```

### Verbose Build Output

```powershell
cmake --build . --config Debug --verbose
```

### Parallel Build (Faster)

```powershell
cmake --build . --config Debug --parallel 8
```

---

## CI/CD Integration

### GitHub Actions (Future)

```yaml
# .github/workflows/build.yml
name: Build & Test

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - uses: ilammy/msvc-dev-cmd@v1
      - run: cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -B build
      - run: cmake --build build
      - run: ctest --test-dir build --verbose
```

### Local CI Simulation

```powershell
# Simulate CI build steps locally:
$ErrorActionPreference = "Stop"

cd "e:\Maqam Classification\MyApp"

# Clean
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

# Configure
cmake -B build -G "Visual Studio 18 2026" -A x64

# Build
cmake --build build --config Release --parallel 4

# Test
cd build
ctest --verbose
cd ..

Write-Host "Build successful!" -ForegroundColor Green
```

---

## Performance Tips

### Faster Rebuilds

```powershell
# Incremental (only changed files)
cmake --build . --config Debug

# Full rebuild (clean first)
cmake --build . --config Debug --clean-first

# Parallel jobs (faster for multi-core)
cmake --build . --config Debug --parallel 8
```

### Faster Initial Configure

```powershell
# Skip pre-compilation
cmake -G "Ninja" -DCMAKE_CXX_SCAN_FOR_MODULES=OFF ..
```

### Release Build Optimization

```powershell
# Optimized for performance
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
```

---

## Uninstall / Clean

### Remove Build Artifacts

```powershell
cd "e:\Maqam Classification\MyApp"
Remove-Item -Recurse -Force build
```

### Restore to Original

```powershell
# Git clean (removes all untracked files)
git clean -fdX

# Or manually:
Remove-Item -Recurse -Force build
Remove-Item -Recurse -Force CMakeFiles
Remove-Item CMakeCache.txt -ErrorAction SilentlyContinue
```

---

## Command Reference

| Task | Command |
|------|---------|
| **Configure (Debug)** | `cmake -G "Visual Studio 18 2026" -A x64 ..` |
| **Configure (Release)** | `cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..` |
| **Build Debug** | `cmake --build . --config Debug` |
| **Build Release** | `cmake --build . --config Release` |
| **Run Tests** | `ctest --verbose` |
| **Run App** | `.\Debug\MyApp.exe` |
| **Clean** | `cmake --build . --target clean` |
| **Full Rebuild** | `cmake --build . --clean-first` |
| **Help** | `cmake --build . --help` |

---

## Next Steps

1. ✅ **Build the project** (follow steps above)
2. ✅ **Run tests** (`ctest --verbose`)
3. ✅ **Launch app** (`.\Debug\MyApp.exe`)
4. ✅ **Scan for plugins** (click "Scan for Plugins" button)
5. ✅ **Load your VST** (select from list, click "Load Selected Plugin")
6. ✅ **Test audio** (route audio through plugin)

---

## Getting Help

**Error during build?**

1. Check the error message in the output
2. See "Troubleshooting" section above
3. Review `cmake_config.log` or `cmake_build.log` (generated during build)
4. Check `IMPLEMENTATION.md` for architecture details

**CMake documentation:**
https://cmake.org/cmake/help/latest/

**JUCE documentation:**
https://docs.juce.com/master/

---

**Happy building!** 🎵
