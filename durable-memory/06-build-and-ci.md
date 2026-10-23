# 06 — Build, Static Linking & 5-Platform CI

## Build System

- CMake (`>= 3.24`) builds two static targets (`soundpipe_core` and `daisysp`) and links them into the `lmaudio` executable.
- `schema/lmaudio.dsl.json` and `THIRD_PARTY_NOTICES` are hex-embedded into `generated/schema_embedded.hpp` and `generated/notices_embedded.hpp` at configure time with `CMAKE_CONFIGURE_DEPENDS` so changes trigger rebuilds automatically.

## 5-Platform CI & Release Matrix

`.github/workflows/ci.yml` and `.github/workflows/release.yml` build, test, and package standalone archives across:
- `linux-x86_64` (`-static`)
- `linux-arm64` (`-static`)
- `macos-x86_64`
- `macos-arm64`
- `windows-x86_64` (`-static -static-libgcc -static-libstdc++`)
