# Material Design Icons

`materialdesignicons-webfont.ttf` and `LICENSE` are taken unchanged from the npm package
`@mdi/font` 7.4.47 (`fonts/` and the package root), by Pictogrammers. It is the editor's icon font
(D-277, replacing Font Awesome Free Solid of D-96): the editor merges it into the UI font at
start-up so icon glyphs sit next to the text glyphs.

The font holds 7,448 icons at U+F0001..U+F1D17 (one placeholder, `blank`, at U+F68C). Those code
points are above U+FFFF, so ImGui is built with `IMGUI_USE_WCHAR32` (`ThirdParty/imgui/imconfig.h`).

License: the font and the icons are licensed under the Apache License 2.0; see `LICENSE`.

Icon names and code points: <https://pictogrammers.com/library/mdi/>, or the package's
`css/materialdesignicons.css`. The glyphs the editor uses are listed in
`Include/JBro/Editor/EditorIcons.h`; nothing else of the font is referenced.
