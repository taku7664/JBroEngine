# Android Platform Build Contract

`BuildScripts/BuildGame.ps1 -Platform Android` generates a Gradle project inside the selected package output directory.

The current Android package step is Android-first staging. It does not yet build the native engine library. The script expects an existing native library named `libJBroGame.so` and fails before Gradle if the library is missing.

Native library lookup order:

- Project root: `Build/Android/<abi>/<Configuration>/libJBroGame.so`
- Project root: `Build/Android/<Configuration>/<abi>/libJBroGame.so`
- Engine root: `Build/Android/<abi>/<Configuration>/libJBroGame.so`
- Engine root: `Build/Android/<Configuration>/<abi>/libJBroGame.so`
- Engine root: `PlatformBuild/Android/libs/<abi>/libJBroGame.so`

Generated Gradle project payload contract:

- `app/src/main/assets/Content/build_manifest.jbmanifest`
- `app/src/main/assets/Content/game_assets.jbpack`
- `app/src/main/jniLibs/<abi>/libJBroGame.so`

Forbidden package artifacts:

- loose `Content/Assets`
- `SDK`
- `Editor`
- `Localization`
- `GameScript.dll`

Next required step:

- Add an Android NDK native target that produces `libJBroGame.so` through the same runtime bootstrap and Vulkan RHI contract.
