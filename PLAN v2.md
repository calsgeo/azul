## Plan: UrbanFunctionArea Styling + Appearance Toggle + Texture Visibility Fix (macOS)

### Summary
- Keep `UrbanFunctionArea` support and appearance workflow.
- Change `UrbanFunctionArea` default fill from near-white to a darker light gray: `(0.81, 0.81, 0.81, 1.0)` (15% darker than `0.95` baseline).
- Keep border black at current 1px thickness for this iteration.
- Focus texture issue fixes on runtime visibility/access and control discoverability (parser already produces textured buffers for Railway and Buildings).

### Key Implementation Changes
1. UrbanFunctionArea visual defaults:
- Add/update `UrbanFunctionArea` semantic default color to `(0.81, 0.81, 0.81, 1.0)`.
- Mirror the same value in “Reset to Defaults”.
- Keep border rendering through existing edge pipeline in black, 1px.

2. Texture visibility reliability (runtime):
- Add post-load diagnostics for textured buffer counts and sample texture paths.
- Add macOS security-scoped access handling for selected GML URL and parent directory during load/initial texture resolution, then release.
- Add one-time texture-load failure logs per path (with error), while preserving fallback rendering.
- Clear failed-texture cache on new load and when textures are toggled back on.

3. Appearance control discoverability:
- Make View-menu insertion robust (anchor from known outlets/items, not brittle title-only logic).
- Add toolbar `Textures` toggle bound to the same action/state as View menu.
- Keep `azulShowTextures` as single source of truth and keep `.azulview` round-trip support.

### Test Plan
1. `Wijk_Buurten_UrbanFunctionArea_EnergyADEv3.gml`:
- `UrbanFunctionArea` fill is `(0.81, 0.81, 0.81, 1.0)`.
- Borders are black and visible.

2. `Railway_Scene_LoD3.gml` and `Buildings.gml`:
- Textures render when `Show Textures = ON`.
- Deterministic fallback (material/type color) when OFF or texture read fails.
- ON/OFF toggle restores behavior without restart.

3. UI and persistence:
- View menu toggle and toolbar button both visible and synchronized.
- Restart restores `azulShowTextures`.
- Save/load `.azulview` restores texture toggle state.

4. Regression:
- Picking, selection overlay, LoD filtering, visibility toggles, and edge rendering unchanged.

### Assumptions
- macOS scope only for this iteration.
- Border thickness remains 1px by prior decision.
- “15% darker” is applied relative to the previous `0.95` plan baseline, resulting in `0.81` per RGB channel.
