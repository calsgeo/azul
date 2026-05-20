### Energy ADE Deep Support With Same-Folder XSD Discovery

### Summary
- I agree with your indication: ADE schema handling should be simplified so users place ADE XSD files next to the CityGML file.
- We will implement schema-driven ADE parsing with this rule:
  - discover ADE XSDs by scanning `.xsd` files in the **same folder** as the opened CityGML,
  - match by `targetNamespace` against ADE namespaces declared in the CityGML,
  - if no match is found, **warn and continue** with generic ADE fallback parsing.

### Implementation Changes
- Add a schema discovery stage at CityGML parse start in `GMLParsingHelper`:
  - read namespace declarations from `CityModel`,
  - scan sibling-folder `.xsd` files,
  - parse `targetNamespace` from each XSD and build `namespace -> schema file` map,
  - activate ADE class registries only for matched namespaces (Energy ADE now, extensible to others like UtilityNetwork ADE).
- Replace prefix-specific ADE detection (`nrg3`) with namespace-driven ADE detection:
  - ADE object creation logic uses matched schema class sets,
  - nested ADE classes (e.g., `PartyWallSurface`, schedules/resources/timeseries classes) become explicit hierarchy nodes instead of flatten-only behavior.
- Keep robust fallback behavior:
  - if matched XSD is missing for an ADE namespace, log/status warning and continue with current generic namespaced parsing.
- Preserve current UX defaults:
  - opening a file still uses default visualization first (no forced appearance activation),
  - appearance handling remains opt-in and independent from schema discovery.
- Extend for future ADEs without code forks:
  - schema parser and class-registry loading are namespace-agnostic,
  - adding a new ADE becomes “drop matching XSD into the CityGML folder”.

### Public Interface / Behavior Changes
- Add parser diagnostics surfaced to status/log:
  - discovered XSD files,
  - matched ADE namespaces,
  - unmatched ADE namespaces (warning),
  - fallback mode active/inactive.
- No mandatory new user dialogs for schema selection; discovery is automatic from same folder.

### Test Plan
- Positive schema match:
  - place `Energy_ADE_3.0_beta7.xsd` beside `Alderaan_Energy_ADE_Core_Building_physics_CoincidesWithLoD2Hull.gml`,
  - verify ADE class nodes (including `PartyWallSurface`) appear as explicit objects.
- Missing schema:
  - remove/rename XSD, open same file,
  - verify file still loads and warning is emitted.
- Multi-ADE readiness:
  - open file containing multiple ADE namespaces with corresponding XSDs in same folder,
  - verify namespace-specific schema matches are reported and parsed.
- Regression:
  - open non-ADE CityGML files (`Buildings.gml`, `Terrain.gml`) and confirm unchanged load/render behavior.

### Assumptions
- “Same folder” is strict: no recursive subfolder search for XSDs.
- Internet/schema download is not used; only local `.xsd` files are considered.
- Namespace matching by `targetNamespace` is authoritative (not filename-based).
