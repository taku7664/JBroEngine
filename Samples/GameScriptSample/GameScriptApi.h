#pragma once

// GameScript is built in two ways:
// - Windows Editor: dynamic library for live compile / hot reload.
// - Windows Game and Web Game: statically linked into the final binary/wasm.
//
// Keep export decoration out of gameplay code so the same source files can be
// compiled by MSVC and Emscripten.
#if defined(_WIN32) && (defined(GAMESCRIPT_EXPORTS) || defined(_USRDLL))
#define GAMESCRIPT_API __declspec(dllexport)
#else
#define GAMESCRIPT_API
#endif
