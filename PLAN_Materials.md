### Fix Missing `X3DMaterial` Appearance Application (Energy ADE)

### Summary
- I agree with your indication: materials are parsed but not propagated to the rendered polygons.
- We will fix this in `GMLParsingHelper` by resolving container-level material targets (`...Solid`, `...MultiSurf`, `..._lod*_geom`) to descendant polygon IDs, and by restoring safe xlink traversal where needed.

### Key Changes
- **Material parsing model update (`GMLParsingHelper.hpp`)**
  - Replace single-value `materialBySurfaceId` with multi-entry storage per target:
    - `targetId -> [ParsedMaterialAssignment]`
    - `ParsedMaterialAssignment` includes: `ParsedMaterial`, `isFront`, `parseOrder`.
  - Parse `app:isFront` in `parseX3DMaterial`.
  - Conflict rule (chosen): for same target, prefer `isFront=true`; if equal, prefer last declaration.
- **Target-to-polygon resolution**
  - Add resolver utilities:
    - `resolveTargetToPolygonIds(targetId, nodesById)`
    - recursive collector over XML nodes + `xlink:href` traversal with visited-set cycle protection.
  - Accept direct `Polygon/Triangle` targets and container targets (`Solid`, `MultiSurface`, `lod*` geometry wrappers, etc.).
  - Cache resolved target expansions (`targetId -> polygonIds`) for performance.
- **Appearance assignment pipeline**
  - Before `applyAppearancesToObject`, precompute:
    - `materialByPolygonId` from expanded targets.
    - `textureByPolygonId` from expanded targets (reuse existing ring UV logic).
  - In `applyAppearancesToObject`, match polygon in this order:
    - direct polygon-id mapping,
    - ring-id inferred mapping (existing fallback),
    - no style.
  - Keep current runtime toggle behavior (`useAppearances`, selected theme) unchanged.
- **Safe xlink recovery for geometry references**
  - Re-enable xlink follow-up in the flattening geometry branch (`baseSurface/surfaceMember/...`) with visited-set guard to avoid loops/dup storms.
- **Diagnostics**
  - Add parse-time logs:
    - parsed material targets,
    - resolved target expansions,
    - unresolved targets,
    - polygons with assigned style.

### Validation Plan (using `Test_data`)
- `Alderaan_Energy_ADE_Core_Building_Physics_UsageZone.gml`
  - Enable Appearances and verify visible material changes for:
    - building `lod1/lod2` solids,
    - usage-zone solids,
    - thermal-opening `lod3` geometry.
- `Alderaan_Energy_ADE_Core_Building_physics_CoincidesWithLoD2Hull.gml`
  - Verify materials on LoD2 hull-related targets (including xlinked geometry references).
- `Alderaan_Energy_ADE_All.gml`
  - Broad regression: appearance activation changes visible output and no crash/loop.
- Baseline regression:
  - `Buildings.gml`, `Terrain.gml` still load with default visualization and optional appearance toggle behavior unchanged.

### Assumptions
- Current renderer stays single-style-per-polygon (no dual front/back shading in one draw path).
- If a target cannot be resolved to polygons, it is logged and skipped, not fatal.
- Theme dropdown behavior remains as-is for this patch; this patch focuses on making existing materials actually appear.
