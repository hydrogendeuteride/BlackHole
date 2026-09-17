**Build & Environment**

- Prerequisites
  - Vulkan SDK installed and `VULKAN_SDK` set.
  - CMake installed.
  - Linux/WSL: `clang`, `clang++`, `make`.
  - Linux desktop: Wayland, EGL, XKB, and SDL platform development files.
    Native Wayland is preferred in Wayland sessions so compositor-managed IME
    composition can reach the engine, with X11 retained as a fallback.
  - Windows clang-cl: Visual Studio with C++ and CMake components.
    `build.py` locates these tools directly without a developer shell.

- Quick build
  ```bash
  python3 ./build.py
  python3 ./build.py debug
  py .\build.py
  py .\build.py debug
  ```
  - Format: `build.py [debug|release] [linux|windows]`
  - Add `--no-shaders` to skip shader compilation.

- Output
  - Runtime output defaults to repo-root `bin/`.
  - Linux/WSL: `./bin/blackhole`
  - Windows single-config: `bin\blackhole.exe`
  - Windows multi-config: `bin\Debug\blackhole.exe` or `bin\Release\blackhole.exe`
  - To keep runtime artifacts inside the build tree instead: `-DVULKAN_ENGINE_OUTPUT_TO_SOURCE_ROOT=OFF`

- compile_commands.json
  - If you need it, enable it explicitly in your build dir with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

- CMake presets
  ```bash
  cmake --preset linux-clang-release
  cmake --build --preset linux-clang-release
  ```
  ```powershell
  py .\build.py release windows
  ```

- Shaders
  - CMake compiles `shaders/*.vert|*.frag|*.comp` to `bin/shaders/` on build.
  - Override generated shader output with `-DVULKAN_ENGINE_SHADER_OUTPUT_DIR=<dir>`.
  - Manual helper: `./compile_shaders.py --config Debug|Release`

- Validation Layers
  - Enabled in Debug in `src/core/config.h`.
  - Use Release to turn them off for normal runtime testing.
