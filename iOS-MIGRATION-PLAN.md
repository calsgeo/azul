# azul iOS Migration Plan

## Overview

Port the macOS 3D city model viewer (AppKit + Metal) to iOS (UIKit + Metal).

## Architecture

### macOS (current)
```
Controller.swift (NSApplicationDelegate)
  ├── NSWindow → NSSplitView
  │     ├── NSOutlineView (sidebar: object hierarchy)
  │     ├── MTKView (3D rendering)
  │     └── NSTableView (attributes)
  ├── NSToolbar (Open, Search, LOD, toggles)
  └── NSWindow (Preferences: Rendering, Selection, Semantic Surfaces)

DataManagerWrapperWrapper (ObjC++ bridge)
  ├── Conforms to NSOutlineViewDataSource/Delegate
  ├── Conforms to NSTableViewDataSource/Delegate
  └── Owns C++ DataManager

DataManager (C++17)
  ├── Parsing (GML, JSON, JSONL, OBJ, OFF, POLY)
  ├── CGAL triangulation
  ├── Edge generation
  ├── LOD filtering
  └── GPU buffer building

MetalView.swift (MTKView subclass)
  ├── Metal pipeline (device, queues, states)
  ├── NSEvent-based camera controls
  ├── Drag-and-drop (NSDraggingInfo)
  └── GPU-based picking
```

### iOS (current)
```
AppDelegate.swift (UIApplicationDelegate) — window managed directly (no scenes)
  └── MainViewController.swift (UIViewController + MTKViewDelegate)
        ├── MTKView (full-screen, continuous rendering, MSAA disabled on sim)
        ├── Floating UI buttons (Open, Objects, Edges, BBox, Home)
        ├── UIGestureRecognizer camera controls (pan/orbit, pan/truck, pinch/zoom, rotate, tap/pick)
        ├── Full Metal rendering pipeline (lit, unlit, edge, picking)
        ├── GPU-based picking (offscreen render target + blit readback)
        ├── UIDocumentPickerViewController (file loading)
        ├── ObjectListViewController (popover/modal)
        │     └── UITableView with expandable cells
        └── AttributeTableViewController (popover/modal)
              └── UITableView (key-value attributes)

DataManagerWrapperWrapper (ObjC++ bridge)
  ├── #if TARGET_OS_OSX — macOS outline/table methods
  └── #if !TARGET_OS_OSX — iOS tree navigation methods
        └── Depth-tracking, expand/collapse set via AzulObjectIterator

DataManager (C++17) — UNCHANGED
Metal Shaders (Shaders.metal) — UNCHANGED
```

## Progress

### ✅ Phase 0 — Infrastructure (Complete)

**iOS arm64 static libraries built:**

| Library | Source | Location |
|---------|--------|----------|
| `libpugixml.a` | `libs src/pugixml-1.15/` | `libs-ios/libpugixml.a` (322 KB) |
| `libgmp.a` | `libs src/gmp-6.3.0/` | `libs-ios/libgmp.a` (923 KB) |
| `libmpfr.a` | `libs src/mpfr-4.2.2/` | `libs-ios/libmpfr.a` (856 KB) |
| `libboost_thread.a` | `libs src/boost_1_91_0/` | `libs-ios/libboost_thread.a` (1.0 MB) |
| `libboost_chrono.a`, `libboost_date_time.a`, `libboost_atomic.a`, `libboost_container.a`, `libboost_exception.a` | (Boost deps) | `libs-ios/` |

**iOS simulator libs also available in** `libs-ios-sim/` (separate from device libs since both use `arm64`; lipo cannot combine them).

**Build methods:**
- **GMP**: `./configure --host=arm64-apple-ios --build=arm64-apple-darwin --disable-assembly --disable-shared --enable-static` with `CC="clang -target arm64-apple-ios13.0 -isysroot $(SDK_PATH)"`
- **MPFR**: Same approach, with `--with-gmp=/path/to/gmp-install`
- **pugixml**: Direct `clang++ -arch arm64 -isysroot $(SDK_PATH)` compilation of single `.cpp`
- **Boost**: `b2 toolset=darwin cxxflags="..." linkflags="..." variant=release link=static --with-thread`

**Xcode target** — Created `azul-iOS` target in existing `azul.xcodeproj`.

### ✅ Phase 1 — iOS UI Shell (Complete)

**New files (`src_iOS/`):**

| File | Purpose |
|------|---------|
| `AppDelegate.swift` | `@main` entry point, creates window directly (not via UISceneDelegate due to Info.plist generation issues) |
| `SceneDelegate.swift` | (Created but not used — UISceneDelegate requires proper Info.plist scene manifest) |
| `MainViewController.swift` | Root VC: full-screen MTKView, floating buttons, gesture recognizers, file loading pipeline |
| `ObjectListViewController.swift` | Expandable `UITableView` for object hierarchy (popover on iPad, modal on iPhone) |
| `AttributeTableViewController.swift` | Attributes `UITableView` |
| `Azul-Bridging-Header.h` | Imports `DataManagerWrapperWrapper.h`, `PerformanceHelperWrapperWrapper.h` |

**Modified bridge files:**

| File | Change |
|------|--------|
| `DataManager/DataManagerWrapperWrapper.h` | `#if TARGET_OS_OSX` around AppKit protocols; exposed `AzulObjectIterator` to Swift; added iOS tree navigation method declarations |
| `DataManager/DataManagerWrapperWrapper.mm` | `#if TARGET_OS_OSX` around AppKit methods; added `depth` to `AzulObjectIterator`; implemented iOS tree navigation (12 methods) |
| `DataManager/TableCellView.h` | Wrapped in `#if TARGET_OS_OSX` |
| `DataManager/TableCellView.m` | Wrapped in `#if TARGET_OS_OSX` |

**Key learnings:**
- Simulator on Apple Silicon supports Metal (`MTLCreateSystemDefaultDevice()` returns a valid device)
- MTKView needs `isPaused = false` on iOS (different default than macOS)
- iOS Info.plist generation via `GENERATE_INFOPLIST_FILE = YES` doesn't produce proper `UISceneConfigurations` — easier to have `AppDelegate` create the window directly
- Simulator and device static libraries both use `arm64` but are incompatible — store in separate directories
- Swift debug builds produce `azul-iOS.debug.dylib` that must be bundled in `Frameworks/` for `simctl install` to work

### ✅ Phase 2 — iOS Data Source Bridge (Complete)

**Tree navigation methods added to bridge:**

| ObjC method | Swift name | Returns |
|-------------|-----------|---------|
| `numberOfParsedFiles` | `numberOfParsedFiles()` | `Int` |
| `iteratorForFileAtIndex:` | `iteratorForFile(at:)` | `AzulObjectIterator` |
| `isItemExpandable:` | `isItemExpandable(_:)` | `Bool` |
| `numberOfChildrenOfItem:` | `numberOfChildren(ofItem:)` | `Int` |
| `childOfItem:atIndex:` | `child(ofItem:at:)` | `AzulObjectIterator` |
| `typeOfItem:` | `type(ofItem:)` | `String?` |
| `identifierOfItem:` | `identifier(ofItem:)` | `String?` |
| `visibleStateOfItem:` | `visibleState(ofItem:)` | `Int8` (char) |
| `setVisibleState:forItem:` | `setVisibleState(_:forItem:)` | — |
| `numberOfAttributesOfItem:` | `numberOfAttributes(ofItem:)` | `Int` |
| `attributeKeyOfItem:atIndex:` | `attributeKey(ofItem:at:)` | `String?` |
| `attributeValueOfItem:atIndex:` | `attributeValue(ofItem:at:)` | `String?` |

**`AzulObjectIterator`** — exposed to Swift via the main header. Stores `depth` for indentation. Uses pointer-based `isEqual:`/`hash` for Set tracking.

### ✅ Phase 3 — iOS Rendering & Gestures (Complete)

**Shared types moved to `Math.swift`** (used by both macOS and iOS targets):
- `Constants`, `Vertex`, `EdgeVertex`, `VertexWithNormal`, `BufferWithColour` structs

**Rendering pipeline in `MainViewController.swift`:**

| Feature | Description |
|---------|-------------|
| Pipeline states | lit, unlit, edge, picking from `Shaders.metal` |
| Depth management | Manual depth texture (avoids MTKView internal depth allocation bug on simulator) |
| Anti-aliasing | `sampleCount = 1` on simulator (MSAA depth textures unsupported on sim); use 4 on device |
| `draw(in:)` | Opaque triangles → transparent triangles → edges → bounding box |
| GPU picking | Offscreen render target + blit encoder readback |
| Depth computation | Plane-fitting for camera corrections |
| GPU buffers | `reloadTriangleBuffers()`, `reloadEdgeBuffers()`, `regenerateBoundingBoxBuffer()` from DataManager |
| State buffers | `updateVisibleStateBuffer()`, `updateSelectionStateBuffer()` |

**Camera controls** — All `UIGestureRecognizer`-based:
| Gesture | Action |
|---------|--------|
| One-finger pan | Orbit (arcball) |
| Two-finger pan | Truck (translate) |
| Pinch | Zoom (FOV) |
| Rotation | Twist (roll) |
| Tap | Pick object (GPU readback) |

**Key learnings:**
- MTKView's built-in depth texture (`depthStencilTexture`) fails on simulator with `.shared` storage mode — create a manual depth texture with `.private` storage
- Simulator MSAA depth textures not supported — disable MSAA on simulator
- `draw(in:)` must be explicitly set to continuous rendering: `isPaused = false, enableSetNeedsDisplay = false`
- `Data(bytes:count:)` initializer deprecated on iOS 26 — use `UnsafeBufferPointer` + `contents().copyMemory()` directly
- ObjC method implementations must be OUTSIDE `#if TARGET_OS_OSX` blocks to be visible to Swift on iOS (declaration alone is not enough)

### 🔲 Phase 4 — Polish

1. Configure `Info.plist` for document types (`CFBundleDocumentTypes`)
2. Handle file access via security-scoped URLs
3. Stub Metal rendering (clear color only) → implement full pipeline (done in Phase 3)
4. Orientation support
5. Adaptive layout for iPhone vs iPad

### 🔲 Phase 5 — Testing & Performance

1. Test on device (simulator rendering works but may differ)
2. Profile memory/performance
3. Touch interaction tuning
4. Add app icon for iOS (asset catalog)

## Cross-Platform Compatibility

### Fully portable (zero changes)
| Component | Files |
|-----------|-------|
| C++ data model | `DataModel.hpp` |
| C++ data manager | `DataManager.{hpp,cpp}` |
| All parsers | `*ParsingHelper.hpp` |
| simdjson | `simdjson.{cpp,h}` |
| Metal shaders | `Shaders.metal` |
| Math helpers + shared structs | `Math.swift` |
| Performance helper | `PerformanceHelper.hpp`, `PerformanceHelperWrapperWrapper.{h,mm}` |

### Conditionally compiled (`#if TARGET_OS_OSX`)
| Component | Files |
|-----------|-------|
| ObjC++ bridge (data sources) | `DataManagerWrapperWrapper.{h,mm}` |
| macOS table cell | `TableCellView.{h,m}` |
| Swift files | `Controller.swift`, `MetalView.swift` (excluded from iOS target) |

### iOS-only
| File | Purpose |
|------|---------|
| `src_iOS/AppDelegate.swift` | iOS entry point, window management |
| `src_iOS/MainViewController.swift` | Root view controller (UI + Metal rendering + gestures + picking) |
| `src_iOS/ObjectListViewController.swift` | Object hierarchy browser |
| `src_iOS/AttributeTableViewController.swift` | Attribute inspector |
| `src_iOS/Azul-Bridging-Header.h` | iOS bridging header |

## Key Design Decisions

**UI Layout**: Full-screen 3D view on all devices. Sidebar and attributes shown as popovers/modal sheets.

**Object hierarchy**: Expandable `UITableView` with indentation and tap-to-expand. Flat array of visible items rebuilt on expand/collapse. Depth stored in `AzulObjectIterator.depth`.

**Toolbar**: Minimal floating `UIButton`s overlaid on the 3D view using SF Symbols.

**File loading**: Files app integration via `UIDocumentPickerViewController` with `asCopy: true`.

**Window management**: AppDelegate creates `UIWindow` directly (avoids `UISceneDelegate` Info.plist configuration issues).

**Target devices**: Both iPhone and iPad with adaptive layout.

## Dependencies

### Static libraries
All libraries built in two variants:
- `libs-ios/` — iOS device (`arm64-apple-ios`, built with `iphoneos` SDK)
- `libs-ios-sim/` — iOS simulator (`arm64-apple-ios-simulator`, built with `iphonesimulator` SDK)

Headers in `include/` are shared with macOS target.

| Library | iOS Build Complexity |
|---------|---------------------|
| pugixml | Trivial (single .cpp) |
| GMP | Medium (autotools cross-compile) |
| MPFR | Medium (autotools cross-compile, depends on GMP) |
| Boost | Medium (b2 with custom toolset) |
| CGAL | Header-only (no build needed) |
| simdjson | Vendored source (no build needed) |
