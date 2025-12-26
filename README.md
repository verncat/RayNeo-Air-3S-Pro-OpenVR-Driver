Just been copied from [OpenVR driver example](../thirdparties/openvr/samples/drivers/drivers/simplehmd/README.md)

SDK: https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR

### Input and Bindings

This driver exposes multiple HMD input components so you can bind RayNeo glasses buttons to SteamVR dashboard actions:

- **System Button (Power/System)**: Toggles Dashboard by default.
- **Volume Up**: Mapped to `trigger/click` (use for Left-Click in Dashboard).
- **Volume Down**: Mapped to `grip/click` (use for Back).
- **Brightness Button**:
  - **Single Click**: Mapped to `application_menu/click` (use for Home).
  - **Double Click**: Recenter the HMD position/orientation (useful for drift correction).

Pointer pose for mouse-like interaction comes from `/user/head/pose/raw`.

Example binding for Generic HMD (place in SteamVR resources or import via Controller Bindings UI): see `resources/bindings/rayneo_generic_hmd.json`.

### Download

You can download the latest release from the [GitHub Releases page](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR/releases).

### Build Instructions

**Prerequisites:**
- **CMake**: Version 3.16 or newer.
- **Compiler**: C++23 compatible compiler.
  - **Windows**: Visual Studio 2022 (v17.0+).
  - **Linux**: GCC 13+ or Clang 16+.
- **Dependencies**:
  - **Windows**: `libusb` (recommended via `vcpkg`).
  - **Linux**: `libusb-1.0-0-dev` (Debian/Ubuntu) or `libusb-devel` (Fedora/Arch), `pkg-config`.

**Windows:**
1. Install `vcpkg` and `libusb` (if not already installed):
   ```powershell
   git clone https://github.com/microsoft/vcpkg
   .\vcpkg\bootstrap-vcpkg.bat
   .\vcpkg\vcpkg install libusb
   ```
2. Open the project folder in VS Code or a terminal.
3. Create a build directory: `mkdir build && cd build`
4. Generate project files (replace path to `vcpkg.cmake` with your actual path):
   ```powershell
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
5. Build the project: `cmake --build . --config Release`

**Linux:**
1. Install dependencies (Ubuntu/Debian example):
   ```bash
   sudo apt-get update
   sudo apt-get install cmake build-essential libusb-1.0-0-dev pkg-config
   ```
2. Open a terminal in the project folder.
3. Create a build directory: `mkdir build_linux && cd build_linux`
4. Generate makefiles: `cmake ..`
5. Build the project: `make`
