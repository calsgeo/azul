# APPEARANCE.md

## Scope

This file documents the appearance implementation work applied in this branch for:

- CityGML appearance support
- CityJSON / CityJSONL appearance support
- Shared macOS UI behavior parity
- Rendering pipeline support for textured and material appearances
- Stability/default-behavior fixes discovered during validation (`Buildings.gml`, `Terrain.gml`, `Railway_Scene_LoD3.gml`, `LoD3_Railway.city.json`)

The goal is identical user interaction for CityGML and CityJSON using the same existing controls.

## Final user-visible behavior

- One shared appearance toggle and one shared appearance drop-down are used for CityGML and CityJSON.
- Drop-down behavior:
  - No themes available: `No Appearances` (disabled)
  - Themes available: `All Appearances` + themes (enabled)
- Toolbar/menu label uses `Appearances`.
- Default file load behavior is non-textured default visualization (appearance toggle off), unless the user enables appearances.
- For CityJSON when both material and texture styles exist:
  - `Materials` and `Textures` modes are exposed
  - raw `visual` is removed from the selector
- For CityGML building datasets with thematic surfaces:
  - default view shows thematic roof/wall/ground colors without requiring appearance toggle
  - duplicate `lod*` building shell rendering is suppressed to avoid gray/white washout

## Files changed and detailed implementation

### 1) `src/DataManager/DataModel.hpp`

Added appearance-aware geometry fields:

- `AzulRing`
  - `textureCoordinates: std::vector<std::array<float, 2>>`
  - `hasTextureCoordinates`
- `AzulPolygon`
  - `appearanceStyleId`
- `AzulTriangle`
  - `textureCoordinates[3][2]`
  - `hasTextureCoordinates`
  - `appearanceStyleId`
- New `AzulAppearanceStyle`
  - `hasTexture`
  - `textureUri`
  - `hasMaterial`
  - `theme`
  - `materialColour[4]`
- `AzulObject`
  - `appearanceStyles`
  - `appearanceThemes`
- `TriangleBuffer`
  - `textureUri`

All copy constructors were kept consistent with the new fields.

---

### 2) `src/DataManager/DataManager.hpp`

Appearance API surface for UI/bridge:

- `std::vector<std::string> getAvailableAppearanceThemes()`
- `void setUseAppearances(bool enabled)`
- `void setAppearanceTheme(const std::string &theme)`
- appearance state:
  - `bool useAppearances`
  - `std::string appearanceTheme`

---

### 3) `src/DataManager/DataManager.cpp`

#### 3.1 Triangulation and UV propagation

- Polygon `appearanceStyleId` is propagated to created triangles.
- Ring UV coordinates are mapped into per-triangle UVs (`triangle.textureCoordinates` + `hasTextureCoordinates`).

#### 3.2 Triangle buffer keying and appearance-aware rendering

- Introduced buffer keying with:
  - semantic type
  - texture URI
  - RGBA
- When appearances are enabled:
  - applies style material color (theme-filtered)
  - applies texture URI only when UVs exist and texture URI is valid
- Added mode support:
  - `Materials`
  - `Textures`

#### 3.3 Theme availability logic

- `getAvailableAppearanceThemes()` now inspects style content.
- If both material and texture styles are present:
  - adds `Materials`
  - adds `Textures`
  - removes `visual`

#### 3.4 Default-behavior fixes for CityGML datasets

- **LoD Highest fallback fix**:
  - under `__highest__`, non-LoD leaves with geometry are kept visible.
  - prevents full disappearance for datasets such as `Terrain.gml` when no explicit LoD tag exists on rendered nodes.
- **Building shell suppression**:
  - when rendering under `Building` / `BuildingPart` context, `lod*Solid` / `lod*MultiSurface` shell nodes are skipped.
  - avoids duplicate white/gray shells masking thematic surface colors in default mode.

#### 3.5 Default material fallback for specific Building/BuildingPart cases

- In default (appearances off) mode, if triangles in `Building` / `BuildingPart` context carry material-only styles, material color can still be used (textures remain toggle-controlled).

---

### 4) `src/DataManager/GMLParsingHelper.hpp`

Implemented CityGML appearance parsing and mapping:

#### 4.1 Style pooling and assignment maps

- Added style pool + deduplication key map
- Added target maps:
  - polygon-id -> style-id
  - ring-id -> style-id
  - ring-id -> UV coordinates

#### 4.2 Appearance parsing support

- Parses:
  - `Appearance`
  - `X3DMaterial`
  - `ParameterizedTexture`
  - `TexCoordList`
- Tracks themes and finalizes:
  - `parsedFile.appearanceStyles`
  - `parsedFile.appearanceThemes`

#### 4.3 Target resolution and xlink handling

- Adds reference normalization and recursive target traversal to resolve style targets and geometry targets.
- Supports style assignment through referenced nodes and ring references.

#### 4.4 Texture URI resolution

- Texture URI resolution is relative to source file path.
- Added folder-name fallback:
  - tries both `appearance/` and `appearances/`

#### 4.5 Polygon/ring integration

- Applies style IDs by polygon ID or exterior ring fallback.
- Applies ring UV arrays and closure normalization.

---

### 5) `src/DataManager/JSONParsingHelper.hpp`

Implemented CityJSON appearance parsing and style synthesis:

#### 5.1 Appearance context

- Adds document-level context for:
  - `materials`
  - `textures`
  - `vertices-texture`
  - `default-theme-material`
  - `default-theme-texture`

#### 5.2 Theme/value extraction

- Supports both assignment forms:
  - `value`
  - `values` (nested)
- Deterministic theme selection policy:
  - prefer default-theme if present and available
  - otherwise first available key

#### 5.3 Geometry recursion and assignment propagation

- Recursively propagates semantic/material/texture nested arrays through geometry nesting levels.
- Builds per-polygon style IDs and UV assignment from texture ring data.

#### 5.4 Style pooling and finalization

- Deduplicates styles and collects parsed themes.
- Finalizes onto `parsedFile`.

#### 5.5 Texture URI resolution

- Relative URI resolution against source file directory.
- Added `appearance` vs `appearances` fallback for file existence.

---

### 6) `src/DataManager/JSONLinesParsingHelper.hpp`

Implemented CityJSON sequence support with appearance parity:

- Reuses JSON appearance context logic.
- Supports root `CityJSON` appearance definition.
- Supports per-feature `CityJSONFeature` appearance override/context.
- Finalizes merged style/theme pool into parsed file.

---

### 7) `src/DataManager/DataManagerWrapperWrapper.h`
### 8) `src/DataManager/DataManagerWrapperWrapper.mm`

Bridge additions exposed to Swift:

- `setUseAppearances:`
- `setAppearanceTheme:`
- `availableAppearanceThemes`
- triangle-buffer texture URI accessors used by Metal upload path

---

### 9) `src/Shaders.metal`

Added textured render path:

- textured vertex/fragment functions used by MetalView
- UV input support
- texture sampling path with existing selection/highlight compatibility

---

### 10) `src/MetalView.swift`

Added rendering/runtime support for appearance textures:

- textured pipeline state + sampler
- texture loading cache
- failed texture path tracking
- texture priming for current GPU buffers
- fallback to non-textured lit path when texture is missing/unloadable

Permission/access diagnostics and helpers:

- records permission-denied texture directories
- exposes failed/denied texture directories to controller
- supports retry after access grant

---

### 11) `src/Controller.swift`

Unified appearance UX for all formats:

#### 11.1 Shared controls

- Added toolbar toggle (`Appearances`)
- added toolbar theme popup (`Appearances`)
- added View menu item (`Show Appearances`)

#### 11.2 Selector behavior

- no themes: `No Appearances`, disabled
- themes: `All Appearances` + sorted themes
- removes `visual` from selector when both `Materials` and `Textures` are present

#### 11.3 Rendering refresh flow

- centralized `refreshAppearanceRendering(requestAuthorization:)`
- updates DataManager flags/theme
- regenerates triangle buffers and selection/visibility states
- primes textures when required

#### 11.4 Load/default behavior

- on model file load, resets appearance toggle to off by default
- keeps default Azul color rendering first, user can enable appearances explicitly

#### 11.5 Security-scoped texture access support (macOS)

- retained security-scoped URLs for loaded files/directories
- prompts for texture directory authorization when texture loads fail due to permissions
- re-primes textures and redraws after successful grant
- releases retained scopes on app terminate / new-session reset

#### 11.6 LoD menu guard

- when no LoDs are available, explicitly clears filter (`""`) instead of leaving stale `__highest__`

---

## Validation summary (manual/probe)

### CityJSON

- `Test_data/LoD3_Railway.city.json`
  - materials and textures parsed
  - texture paths resolved with folder-name fallback
  - UI themes normalized for `Materials`/`Textures`

### CityGML

- `Test_data/Railway_Scene_LoD3.gml`
  - textured styles and themes detected
- `Test_data/Terrain.gml`
  - geometry parsed and triangulated
  - visible under highest LoD path after fallback fix
- `Test_data/Buildings.gml`
  - thematic surfaces (`RoofSurface`, `WallSurface`, `GroundSurface`) preserved in default rendering
  - duplicate building shell buffers suppressed in building context to avoid gray/white washout

---

## Known design decisions

- Appearance toggle controls texture/material appearance application globally.
- Default load behavior intentionally starts with appearances off.
- Building/BuildingPart shell suppression is targeted at default semantic-color correctness for datasets containing both shells and thematic surfaces.

---

## Integration checklist for main branch

1. Apply all changes in the files listed above.
2. Build macOS target.
3. Validate with:
   - `Test_data/LoD3_Railway.city.json`
   - `Test_data/Railway_Scene_LoD3.gml`
   - `Test_data/Terrain.gml`
   - `Test_data/Buildings.gml`
4. Confirm:
   - same Appearance controls for CityGML/CityJSON
   - default semantic colors are correct without pressing Appearance
   - toggling Appearance enables textured/material appearance behavior
