# Building Silent Riot

Silent Riot builds with CMake (3.22 or newer) and a C++17 compiler. CMake downloads JUCE
**7.0.12** (and, for tests, Catch2 **v3.5.2**) with `FetchContent` on the first configure,
so that first configure needs network access and takes a few minutes.

It builds two formats: **VST3** and **Standalone**.

## Linux (Ubuntu)

Install the build dependencies once:

```bash
./scripts/install-deps.sh
```

This installs the compiler, CMake, Ninja, ccache and the ALSA, JACK, X11, FreeType,
OpenGL, WebKitGTK and curl development packages JUCE needs (it uses `sudo apt-get`).

Then build:

```bash
./scripts/build.sh            # Debug, into build/
./scripts/build.sh release    # Release, into build-release/
```

Both use Ninja and write `compile_commands.json` (symlinked into the repo root for clangd).

### Where the build lands

With `<dir>` = `build` or `build-release` and `<type>` = `Debug` or `Release`:

| Output | Path |
|---|---|
| VST3 bundle | `<dir>/SilentRiot_artefacts/<type>/VST3/Silent Riot.vst3` |
| Standalone | `<dir>/SilentRiot_artefacts/<type>/Standalone/Silent Riot` |
| VST3 copy | `build/install/VST3/` (every build copies the plugin here; set by `VST3_COPY_DIR` in `CMakeLists.txt`) |

Run the standalone directly:

```bash
"./build-release/SilentRiot_artefacts/Release/Standalone/Silent Riot"
```

To use the VST3 in a host, copy the bundle to `~/.vst3/` (the standard per-user VST3
folder on Linux) and rescan plugins in the host:

```bash
mkdir -p ~/.vst3
cp -r "build-release/SilentRiot_artefacts/Release/VST3/Silent Riot.vst3" ~/.vst3/
```

## Windows

You need the **Visual Studio Build Tools** (or Visual Studio) with the C++ workload, which
also bundles CMake and Ninja, plus Git.

The simplest route is the one CI uses (`.github/workflows/build.yml`): the Visual Studio
generator, from any shell where `cmake` is on the PATH:

```bat
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release
```

`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` links the C runtime statically, which is what
the released binaries use, so the plugin doesn't need the Visual C++ redistributable.

To build with Ninja instead, open an **x64 Native Tools Command Prompt** (or run
`vcvars64.bat` from the Build Tools' `VC\Auxiliary\Build` folder first), then:

```bat
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

If `cmake` is not on the PATH, the copy bundled with the Build Tools is at
`<Build Tools>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
A `vswhere.exe is not recognized` line during configure is harmless.

The VST3 lands in `<dir>\SilentRiot_artefacts\Release\VST3\Silent Riot.vst3`; copy it to
`C:\Program Files\Common Files\VST3\` to make it visible to hosts.

## CMake options

| Option | Default | What it does |
|---|---|---|
| `SR_BUILD_TESTS` | `OFF` | Builds the three Catch2 test executables and registers them with CTest. See [Testing](testing.md) |
| `SR_BUILD_TOOLS` | `OFF` | Builds the tools: `sr_render` (headless renders for the tone sign-off), `sr_snapshot` (EFFECTS panel PNG) and `sr_editor_snapshot` (full-editor PNG for the manual). See [`../../tools/README.md`](../../tools/README.md) |

For example, a Release build with tests:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Troubleshooting

- **Configure fails while fetching JUCE or Catch2.** The first configure clones them from
  GitHub. Check your network or proxy, then re-run the configure.
- **Reconfiguring is slow on every clean build.** Set `FETCHCONTENT_BASE_DIR` to a folder
  outside the build tree (CI does this) so JUCE is cloned once and reused.
- **A new `.cpp` file builds but fails to link `sr_render` or `sr_editor_snapshot`.**
  `tools/CMakeLists.txt` keeps its own copy of the plugin's source list for each of them;
  add the file to both (`verify.sh` check 2 flags a missing one).
  `bash scripts/verify.sh --quick` catches this.
- **The plugin doesn't appear in the host.** Check the bundle was copied to the host's
  VST3 folder (above) and force a rescan; some hosts cache plugin lists.
