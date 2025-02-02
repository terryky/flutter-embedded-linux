# How to build Flutter Engine embedder

See also:
- [Custom Flutter Engine Embedders](https://github.com/flutter/flutter/wiki/Custom-Flutter-Engine-Embedders)
- [Custom Flutter Engine Embedding in AOT Mode](https://github.com/flutter/flutter/wiki/Custom-Flutter-Engine-Embedding-in-AOT-Mode)

## 1. Install build tools

```
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
$ export PATH=$PATH:$(pwd)/depot_tools
```

### Python 2

Python 2 is required to build. If you default installation is Python 3 you could workaround this by using virtualenv:

```Shell
$ virtualenv .env -p python2
$ source .env/bin/activate
```

See also: https://github.com/dart-lang/sdk/wiki/Building#python-2

## 2. Create .gclient file

### When using the latest version

```yaml
solutions = [
  {
    "managed": False,
    "name": "src/flutter",
    "url": "https://github.com/flutter/engine.git",
    "custom_deps": {},
    "deps_file": "DEPS",
    "safesync_url": "",
  },
]
```

### When using a specific version

You can check the current engine version (commit id / SHA):
- [master channel](https://raw.githubusercontent.com/flutter/flutter/master/bin/internal/engine.version)
- [dev channel](https://raw.githubusercontent.com/flutter/flutter/dev/bin/internal/engine.version)
- [beta channel](https://raw.githubusercontent.com/flutter/flutter/beta/bin/internal/engine.version)
- [stable channel](https://raw.githubusercontent.com/flutter/flutter/stable/bin/internal/engine.version)

You can also get the engine version from `${path_to_flutter_sdk_install}/flutter/bin/internal/engine.version` of the Flutter SDK which you are currently using.

```yaml
solutions = [
  {
    "managed": False,
    "name": "src/flutter",
    "url": "https://github.com/flutter/engine.git@FLUTTER_ENGINE",
    "custom_deps": {},
    "deps_file": "DEPS",
    "safesync_url": "",
  },
]
```
Note: Replace `FLUTTER_ENGINE` with the commid  it of the Flutter engine you want to use.

## 3. Get source files

```Shell
$ gclient sync
```

## 4. Build embedder

```Shell
$ cd src
```

### for arm64 targets with debug mode

```Shell
$ ./flutter/tools/gn --target-os linux --linux-cpu arm64 --runtime-mode debug --unoptimized --embedder-for-target
$ ninja -C out/linux_debug_unopt_arm64
```

### for arm64 targets with profile mode

```Shell
$ ./flutter/tools/gn --target-os linux --linux-cpu arm64 --runtime-mode profile --no-lto --embedder-for-target
$ ninja -C out/linux_profile_arm64
```

### for arm64 targets with release mode

```Shell
$ ./flutter/tools/gn --target-os linux --linux-cpu arm64 --runtime-mode release --embedder-for-target
$ ninja -C out/linux_release_arm64
```

### for x64 targets with debug mode

```Shell
$ ./flutter/tools/gn --runtime-mode debug --unoptimized --embedder-for-target
$ ninja -C out/host_debug_unopt
```

### for x64 targets with profile mode

```Shell
$ ./flutter/tools/gn --runtime-mode profile --no-lto --embedder-for-target
$ ninja -C out/host_profile
```

### for x64 targets with release mode

```Shell
$ ./flutter/tools/gn --runtime-mode release --embedder-for-target
$ ninja -C out/host_release
```

## 5. Install embedder library

```Shell
$ sudo cp ./out/${path to your selected target and mode}/libflutter_engine.so <path_to_cmake_build_directory>
```

### Supplement

You need to install `libflutter_engine.so` in `<path_to_cmake_build_directory>` to build. But you can switch quickly between debug / profile / release modes for the Flutter app without replacing `libflutter_engine.so` by using `LD_LIBRARY_PATH` when you run the Flutter app.

```Shell
$ LD_LIBRARY_PATH=<path_to_engine> ./flutter-client <path_to_flutter_project_bundle>

# e.g. Run in debug mode
$ LD_LIBRARY_PATH=/usr/lib/flutter_engine/debug/ ./flutter-client ./sample/build/linux/x64/debug/bundle

# e.g. Run in profile mode
$ LD_LIBRARY_PATH=/usr/lib/flutter_engine/profile/ ./flutter-client ./sample/build/linux/x64/profile/bundle

# e.g. Run in release mode
$ LD_LIBRARY_PATH=/usr/lib/flutter_engine/release/ ./flutter-client ./sample/build/linux/x64/release/bundle
```
