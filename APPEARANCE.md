# APPEARANCE Integration Guide (CityGML + CityJSON)

This document lists the **required code changes** to bring appearance support to `main` with the same user behavior for CityGML and CityJSON.

Only appearance functionality is covered.

## 1. Goal and Behavior

- Use one shared appearance workflow for both formats:
  - parse materials/textures + UVs
  - map them to polygons/triangles
  - render via existing Appearance toggle and theme drop-down
- Keep UI behavior identical across formats:
  - same menu/toolbar controls
  - same toggle behavior
  - same theme selector behavior (`No Appearances` / `All Appearances` + themes)
- Keep default non-appearance rendering available.

## 2. Required File-Level Changes

## 2.1 Data Model (shared by all parsers/renderers)

Update [src/DataManager/DataModel.hpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/DataModel.hpp):

- `AzulRing`:
  - add `textureCoordinates` (`std::vector<std::array<float, 2>>`)
  - add `hasTextureCoordinates`
- `AzulPolygon`:
  - add `appearanceStyleId`
- `AzulTriangle`:
  - add `textureCoordinates[3][2]`
  - add `hasTextureCoordinates`
  - add `appearanceStyleId`
- Add `AzulAppearanceStyle` struct:
  - `hasTexture`, `textureUri`
  - `hasMaterial`, `theme`, `materialColour[4]`
- `AzulObject`:
  - add `appearanceStyles`
  - add `appearanceThemes`
- `TriangleBuffer`:
  - add `textureUri`

These fields are mandatory for both CityGML and CityJSON appearance paths.

## 2.2 DataManager Rendering/Buffers

Update [src/DataManager/DataManager.cpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/DataManager.cpp) and [src/DataManager/DataManager.hpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/DataManager.hpp):

- During triangulation:
  - propagate `appearanceStyleId` from polygon to produced triangles
  - propagate UVs from rings into triangle UVs (`hasTextureCoordinates` support)
- Triangle-buffer generation:
  - include texture URI + RGBA in buffer keying (`triangleBufferKey`)
  - when appearances enabled:
    - apply material color from selected style (if theme matches)
    - attach texture URI when UVs exist
- Keep theme filtering through `appearanceTheme` (`""` means all themes)
- Expose appearance controls in DataManager:
  - `setUseAppearances(bool)`
  - `setAppearanceTheme(const std::string &)`
  - `getAvailableAppearanceThemes()`

This is the runtime core used by both CityGML and CityJSON.

## 2.3 ObjC++ Bridge API

Update [src/DataManager/DataManagerWrapperWrapper.h](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/DataManagerWrapperWrapper.h) and [src/DataManager/DataManagerWrapperWrapper.mm](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/DataManagerWrapperWrapper.mm):

- expose triangle buffer texture URI to Swift
- expose appearance control API:
  - `setUseAppearances:`
  - `setAppearanceTheme:`
  - `availableAppearanceThemes`

Without this, UI cannot control appearance state.

## 2.4 Metal Rendering Path

Update [src/MetalView.swift](/Users/cleon/Documents/GitHub/fork_azul/azul/src/MetalView.swift) and [src/Shaders.metal](/Users/cleon/Documents/GitHub/fork_azul/azul/src/Shaders.metal):

- Add textured pipeline (`vertexLitTextured`/`fragmentLitTextured`) in addition to lit/unlit/edge pipelines
- In draw path:
  - use textured pipeline when `showTextures` and buffer has texture path
  - fallback to lit color pipeline when texture missing/failed
- Add texture loading/cache/failure tracking for texture URIs
- Shader changes:
  - `VertexWithNormalIn` includes UV
  - textured fragment samples texture and keeps selection-highlight behavior

This is required for visual texture support (CityGML + CityJSON).

## 2.5 Shared UI Parity (no format differences)

Update [src/Controller.swift](/Users/cleon/Documents/GitHub/fork_azul/azul/src/Controller.swift):

- Keep one shared appearance UI:
  - Appearance toggle (toolbar + View menu)
  - Appearance theme pop-up in toolbar
- Theme options behavior:
  - no themes -> disabled + `No Appearances`
  - themes available -> enabled + `All Appearances` + concrete themes
- Persist and apply appearance state through existing view parameter flow
- On toggle/theme change:
  - call DataManager appearance setters
  - regenerate triangle buffers
  - refresh texture priming + redraw

No additional controls should be introduced for CityJSON.

## 2.6 CityGML Appearance Parser

Update [src/DataManager/GMLParsingHelper.hpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/GMLParsingHelper.hpp):

- Parse CityGML appearance members:
  - materials
  - parameterized textures
  - texture coordinates
  - per-theme tracking
- Resolve image URI paths relative to source file
- Build/deduplicate `AzulAppearanceStyle` entries
- Assign `appearanceStyleId` to polygons
- Assign UV coordinates to rings
- Populate `parsedFile.appearanceStyles` and `parsedFile.appearanceThemes`
- Clear appearance helper state on parse resets

This is the full CityGML-side parser support.

## 2.7 CityJSON Appearance Parser (.json)

Update [src/DataManager/JSONParsingHelper.hpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/JSONParsingHelper.hpp):

- Add appearance parsing context at document scope:
  - `appearance.materials`
  - `appearance.textures`
  - `appearance.vertices-texture`
  - optional `default-theme-material` and `default-theme-texture`
- Parse geometry-level assignments:
  - `material` themes with `value` and `values`
  - `texture` themes with `values`
- Theme selection policy:
  - use default theme if provided and present
  - else deterministic fallback to first available theme key
- Convert assignments into:
  - polygon `appearanceStyleId`
  - ring UV coordinates
  - pooled `appearanceStyles`
  - `appearanceThemes`
- Ensure null semantics entries still keep geometry polygons (no geometry loss)

This is the required CityJSON-side parser support.

## 2.8 CityJSONFeature / JSONL Appearance Parser (.jsonl)

Update [src/DataManager/JSONLinesParsingHelper.hpp](/Users/cleon/Documents/GitHub/fork_azul/azul/src/DataManager/JSONLinesParsingHelper.hpp):

- Reuse same appearance context logic as JSON parser
- Parse root `CityJSON` appearance lines
- Parse per-feature `CityJSONFeature` appearance (local to that feature)
- Finalize pooled styles/themes into `parsedFile` after stream parse

This ensures parity for CityJSON sequence files.

## 3. Main-Branch Integration Order

Apply in this order to reduce merge conflicts:

1. `DataModel.hpp`
2. `DataManager.hpp/.cpp`
3. `DataManagerWrapperWrapper.h/.mm`
4. `Shaders.metal` + `MetalView.swift`
5. `Controller.swift`
6. `GMLParsingHelper.hpp`
7. `JSONParsingHelper.hpp`
8. `JSONLinesParsingHelper.hpp`

## 4. Validation Checklist for PR

Use at least these manual checks:

- CityGML with known appearances:
  - load file
  - toggle appearances on/off
  - choose `All Appearances` and specific theme
  - verify textures/material colors + no crashes
- CityJSON:
  - load `Test_data/LoD3_Railway.city.json`
  - verify drop-down enabled and theme list populated (`visual` expected)
  - toggle appearances and verify rendering changes
- CityJSONFeature (.jsonl):
  - verify appearance works for features with local `appearance`
- Regression:
  - files with no appearance still render correctly and show `No Appearances`
  - missing/invalid texture refs fall back safely (no crash)

## 5. Non-Goals (keep out of this PR)

- No new UI controls specific to CityJSON
- No redesign of appearance interaction model
- No unrelated parser/rendering refactors beyond appearance logic

