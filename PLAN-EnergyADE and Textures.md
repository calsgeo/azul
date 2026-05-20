# Plan: ADE + Appearance Workflow Update With `Railway_Scene_LoD3.gml` and `X3DMaterial` (macOS)

## Summary
Extend the planned appearance workflow to cover both texture and material cases found in `Railway_Scene_LoD3.gml`, not only `ParameterizedTexture`.  
Keep texture path resolution based on `app:imageURI` URI semantics, and add a persistent `Show Textures` toggle in the top `View` menu near LoD.

## Key Changes
- **Appearance parser coverage**
  - Parse `app:Appearance` blocks for both:
    - `app:ParameterizedTexture` with `app:target uri="..."` + `TexCoordList`.
    - `app:X3DMaterial` with `app:target` text content (`#PolyID...` form).
  - Retain polygon and ring IDs from GML geometry to support both link styles.
  - Parse X3D fields used by rendering: `diffuseColor`, `transparency`.
  - Parse but do not drive lighting from `emissiveColor`, `specularColor`, `ambientIntensity`, `shininess`, `isSmooth` in this iteration.

- **Rendering precedence and behavior**
  - Deterministic per-surface precedence:
    - If `Show Textures = ON` and valid texture mapping exists: render textured.
    - Else if X3D material exists: render with material color/alpha.
    - Else: fallback to existing type color.
  - Map CityGML X3D transparency to renderer alpha as `alpha = 1.0 - transparency`.
  - Keep existing selection/visibility/picking behavior via unchanged `objectId` semantics.

- **Texture URI resolve (no hardcoded folders)**
  - Resolve texture source from `app:imageURI`:
    - Absolute URI/path: use directly.
    - Relative path: resolve against loaded GML file directory.
  - No fixed folder assumptions (`citygml_textures`, `appearance`, etc. are data-driven only).
  - Missing/unreadable texture is non-fatal and falls back to material/type color.

- **UI and persistence**
  - Add `Show Textures` toggle to top `View` menu near LoD controls.
  - Persist toggle in `UserDefaults`.
  - Include toggle in `.azulview` save/load model so view files round-trip this state.

- **Energy ADE scope remains**
  - Keep previously agreed Energy ADE cityobject support (cityobjects only), including `nrg3:UrbanFunctionArea`.

## Test Plan
- Load `Wijk_Buurten_UrbanFunctionArea_EnergyADEv3.gml` and verify ADE cityobjects parse without unknown-node failures.
- Load `Buildings.gml` and verify texture mapping from `app:imageURI` relative paths.
- Load `Railway_Scene_LoD3.gml` and verify both appearance types are exercised:
  - `ParameterizedTexture` surfaces render textured when enabled.
  - `X3DMaterial` surfaces render material colors/transparency.
  - With textures disabled, material/type-color rendering is used deterministically.
- Validate persistence:
  - Toggle `Show Textures`, restart app, confirm state restored.
  - Save/load `.azulview`, confirm state restored from file.
- Regression checks:
  - Picking, selection overlay, edge rendering, visibility toggles, and LoD controls still behave as before.

## Assumptions
- macOS-only delivery for this iteration.
- `X3DMaterial` support is rendering-capable via diffuse + transparency now; advanced material terms are parsed but not yet used for custom lighting.
- Appearance precedence is texture > material > type color.
