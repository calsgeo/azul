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

### iOS (target)
```
AppDelegate.swift (UIApplicationDelegate)
  └── SceneDelegate.swift (UISceneDelegate)
        └── MainViewController.swift
              ├── MTKView (full-screen 3D rendering)
              ├── Floating UI buttons (Open, Objects, Edges, BBox, Home)
              ├── UIGestureRecognizer camera controls
              │     ├── UIPanGestureRecognizer (orbit)
              │     ├── UIPinchGestureRecognizer (zoom)
              │     ├── UIRotationGestureRecognizer (twist)
              │     └── UITapGestureRecognizer (select)
              ├── UIDocumentPickerViewController (file loading)
              ├── ObjectListViewController (popover/modal)
              │     └── UITableView with expandable cells
              └── AttributeTableViewController (popover/modal)
                    └── UITableView (key-value attributes)

DataManagerWrapperWrapper (ObjC++ bridge)
  ├── #if TARGET_OS_OSX — macOS outline/table methods
  └── #if !TARGET_OS_OSX — iOS UITableViewDataSource/Delegate (Phase 2)

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
| `libboost_chrono.a` | (Boost dependency) | `libs-ios/libboost_chrono.a` |
| `libboost_date_time.a` | (Boost dependency) | `libs-ios/libboost_date_time.a` |
| `libboost_atomic.a` | (Boost dependency) | `libs-ios/libboost_atomic.a` |
| `libboost_container.a` | (Boost dependency) | `libs-ios/libboost_container.a` |
| `libboost_exception.a` | (Boost dependency) | `libs-ios/libboost_exception.a` |

**Build methods:**
- **GMP**: `./configure --host=arm64-apple-ios --build=arm64-apple-darwin --disable-assembly --disable-shared --enable-static` with `CC="clang -target arm64-apple-ios13.0 -isysroot $(SDK_PATH)"`
- **MPFR**: Same approach, with `--with-gmp=/path/to/gmp-install`
- **pugixml**: Direct `clang++ -arch arm64 -isysroot $(SDK_PATH)` compilation of single `.cpp`
- **Boost**: `b2 toolset=darwin-ios variant=release link=static threading=multi address-model=64 architecture=arm --with-thread`

**iOS Xcode target** (`azul-iOS`) created in `azul.xcodeproj`.

### ✅ Phase 1 — iOS UI Shell (Complete)

**New files (`src_iOS/`):**

| File | Purpose |
|------|---------|
| `AppDelegate.swift` | `@main` entry point, `UIApplicationDelegate` |
| `SceneDelegate.swift` | `UISceneDelegate`, creates window with `MainViewController`, handles `openURLContexts` |
| `MainViewController.swift` | Root VC: full-screen MTKView, floating buttons, gesture recognizers, file loading |
| `ObjectListViewController.swift` | Expandable `UITableView` stub (popover on iPad, modal on iPhone) |
| `AttributeTableViewController.swift` | Attributes `UITableView` stub |
| `Azul-Bridging-Header.h` | Imports `DataManagerWrapperWrapper.h`, `PerformanceHelperWrapperWrapper.h` |

**Modified bridge files:**

| File | Change |
|------|--------|
| `DataManager/DataManagerWrapperWrapper.h` | `#if TARGET_OS_OSX` around AppKit protocol conformances |
| `DataManager/DataManagerWrapperWrapper.mm` | `#if TARGET_OS_OSX` around AppKit method implementations; conditional `azul-Swift.h` import |
| `DataManager/TableCellView.h` | Wrapped entire header in `#if TARGET_OS_OSX` |
| `DataManager/TableCellView.m` | Wrapped entire implementation in `#if TARGET_OS_OSX` |

**Build status:** Both macOS and iOS targets build successfully.

### ⏳ Phase 2 — iOS Data Source Bridge (Next)

Rewrite `DataManagerWrapperWrapper.mm` to expose iOS `UITableViewDataSource`/`UITableViewDelegate` methods. Key changes:

1. Add `#if !TARGET_OS_OSX` sections with `UITableViewDataSource` conformances
2. Create iOS-specific table cell (or reuse `UITableViewCell` default styles)
3. Implement expandable hierarchy for object tree (custom indentation + tap-to-expand)
4. Wire up `ObjectListViewController` and `AttributeTableViewController` to real data
5. Handle visibility toggling (with `UISwitch` accessory views)
6. Handle selection → camera focus (double-tap equivalent)

### 🔲 Phase 3 — iOS Rendering & Gestures

Port `MetalView.swift` for iOS. Key changes:

1. Strip out `NSEvent`-based camera controls → already done in `MainViewController.swift`
2. Add Metal pipeline states (lit, unlit, edge, picking) from macOS MetalView
3. Wire up triangle/edge buffers from DataManager
4. Implement `draw(in:)` with proper rendering
5. Implement GPU-based picking with `UITapGestureRecognizer`
6. Port drag-and-drop → `UIDropInteraction`

### 🔲 Phase 4 — File Handling, Menus & Polish

1. Configure `Info.plist` for document types (`CFBundleDocumentTypes`)
2. Handle file access via security-scoped URLs
3. Port remaining menu actions (Help, About)
4. Add app icon for iOS (asset catalog)
5. Orientation support
6. Adaptive layout for iPhone vs iPad

### 🔲 Phase 5 — Testing & Performance

1. Test on device (simulator doesn't support Metal well)
2. Profile memory/performance
3. Handle memory warnings
4. Touch interaction tuning

## Cross-Platform Compatibility

### Fully portable (zero changes)
| Component | Files |
|-----------|-------|
| C++ data model | `DataModel.hpp` |
| C++ data manager | `DataManager.{hpp,cpp}` |
| All parsers | `*ParsingHelper.hpp` |
| simdjson | `simdjson.{cpp,h}` |
| Metal shaders | `Shaders.metal` |
| Math helpers | `Math.swift` |
| Performance helper | `PerformanceHelper.hpp` |
| Performance wrapper | `PerformanceHelperWrapperWrapper.{h,mm}` |

### Conditionally compiled (`#if TARGET_OS_OSX`)
| Component | Files |
|-----------|-------|
| ObjC++ bridge (data sources) | `DataManagerWrapperWrapper.{h,mm}` |
| macOS table cell | `TableCellView.{h,m}` |
| Swift imports | `Controller.swift`, `MetalView.swift` (excluded from iOS target) |

### iOS-only
| File | Purpose |
|------|---------|
| `src_iOS/AppDelegate.swift` | iOS entry point |
| `src_iOS/SceneDelegate.swift` | Scene/window management |
| `src_iOS/MainViewController.swift` | Root view controller |
| `src_iOS/ObjectListViewController.swift` | Object hierarchy browser |
| `src_iOS/AttributeTableViewController.swift` | Attribute inspector |
| `src_iOS/Azul-Bridging-Header.h` | iOS bridging header |

## Key Design Decisions

**UI Layout**: Full-screen 3D view on all devices. Sidebar and attributes shown as slide-over panels (popovers on iPad, modal on iPhone).

**Object hierarchy**: Expandable `UITableView` cells with indentation and tap-to-expand, mimicking `NSOutlineView` behavior.

**Toolbar**: Minimal floating buttons overlaid on the 3D view (`UIButton` with SF Symbols). Most options in action sheets or context menus.

**Preferences**: Skipped for now — use default values everywhere. Can be added later as a dedicated view controller.

**File loading**: Files app integration via `UIDocumentPickerViewController`. Users open files from Files app or other apps via the share sheet.

**Target devices**: Both iPhone and iPad with adaptive layout.

## Dependencies

All static libraries built as iOS arm64 slices in `libs-ios/`. Headers in `include/` are shared with macOS target.

| Library | iOS Build Complexity |
|---------|---------------------|
| pugixml | Trivial (single .cpp) |
| GMP | Medium (autotools cross-compile) |
| MPFR | Medium (autotools cross-compile, depends on GMP) |
| Boost | Medium (b2 with custom toolset) |
| CGAL | Header-only (no build needed) |
| simdjson | Vendored source (no build needed) |
