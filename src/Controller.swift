// azul
// Copyright © 2016-2026 Ken Arroyo Ohori
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

import Cocoa
import Metal
import MetalKit

struct ViewParameters: Codable {
  var eye: [Float]
  var centre: [Float]
  var fieldOfView: Float
  var modelTranslationToCentreOfRotationMatrix: [Float]
  var modelRotationMatrix: [Float]
  var modelShiftBackMatrix: [Float]
  var modelMatrix: [Float]
  var viewMatrix: [Float]
  var projectionMatrix: [Float]
  var viewEdges: Bool
  var viewBoundingBox: Bool
}

class LeftSplitViewController: NSObject, NSSplitViewDelegate {
  func splitView(_ splitView: NSSplitView, effectiveRect proposedEffectiveRect: NSRect, forDrawnRect drawnRect: NSRect, ofDividerAt dividerIndex: Int) -> NSRect {
    if dividerIndex == 0 {
      let effectiveRect = NSRect(x: 0, y: splitView.subviews[0].bounds.height-5, width: splitView.bounds.width, height: 10)
      return effectiveRect
    }
    return NSZeroRect
  }
  
  func splitView(_ splitView: NSSplitView, resizeSubviewsWithOldSize oldSize: NSSize) {
    let dividerThickness = splitView.dividerThickness
    var objectsRect = splitView.subviews[0].frame
    var attributesRect = splitView.subviews[1].frame
    let newFrame = splitView.frame

    objectsRect.size.width = newFrame.size.width
    objectsRect.origin = CGPoint(x: 0, y: 0)
    objectsRect.size.height = newFrame.size.height - attributesRect.size.height - dividerThickness
    
    attributesRect.size.width = newFrame.size.width
    attributesRect.origin.y = objectsRect.origin.y + objectsRect.size.height + dividerThickness

    splitView.subviews[0].frame = objectsRect
    splitView.subviews[1].frame = attributesRect
  }
}

class SearchFieldDelegate: NSObject, NSSearchFieldDelegate {
  var controller: Controller?
  func controlTextDidChange(_ obj: Notification) {
    let searchField = obj.object as! NSSearchField
    searchField.stringValue.withCString { pointer in
      controller!.dataManager.setSearchString(pointer)
    }
    controller!.objectsSourceList!.reloadData()
  }
}

class OutlineView: NSOutlineView {
  var controller: Controller?
  override func keyDown(with event: NSEvent) {
    switch event.charactersIgnoringModifiers![(event.charactersIgnoringModifiers?.startIndex)!] {
    case " ":
      controller?.dataManager.toggleVisibility(forSelection: controller?.objectsSourceList)
    default:
      super.keyDown(with: event)
    }
  }
}

extension NSToolbarItem.Identifier {
  static let lodSelector = NSToolbarItem.Identifier("azul.lodSelector")
  static let search = NSToolbarItem.Identifier("azul.search")
}

@NSApplicationMain
@objc class Controller: NSObject, NSApplicationDelegate, NSToolbarDelegate {

  @IBOutlet weak var window: NSWindow!
  var splitView: NSSplitView?
  var leftSplitView: NSSplitView?
  @objc var searchField: NSSearchField?
  @objc var lodSegmentedControl: NSSegmentedControl?
  var objectsScrollView: NSScrollView?
  var objectsClipView: NSClipView?
  @objc var objectsSourceList: OutlineView?
  var objectsSourceListColumn: NSTableColumn?
  var attributesScrollView: NSScrollView?
  var attributesClipView: NSClipView?
  @objc var attributesTableView: NSTableView?
  var attributeNamesColumn: NSTableColumn?
  var attributeValuesColumn: NSTableColumn?
  var totalProgress: Double = 0.0
  var statusBarView: NSVisualEffectView?
  var progressIndicator: NSProgressIndicator?
  var statusTextField: NSTextField?
  
  @objc var metalView: MetalView?
  var openFiles = Set<URL>()
  
  @IBOutlet weak var toggleViewEdgesMenuItem: NSMenuItem!
  @IBOutlet weak var toggleViewBoundingBoxMenuItem: NSMenuItem!
  @IBOutlet weak var goHomeMenuItem: NSMenuItem!
  @IBOutlet weak var toggleSideBarMenuItem: NSMenuItem!
  @IBOutlet weak var openFileMenuItem: NSMenuItem!
  @IBOutlet weak var newFileMenuItem: NSMenuItem!
  @IBOutlet weak var copyObjectIdMenuItem: NSMenuItem!
  @IBOutlet weak var findMenuItem: NSMenuItem!
  @IBOutlet weak var loadViewParametersMenuItem: NSMenuItem!
  @IBOutlet weak var saveViewParametersMenuItem: NSMenuItem!
  @IBOutlet weak var toggleFullScreenMenuItem: NSMenuItem!
  
  let dataManager = DataManagerWrapperWrapper()!
  let performanceHelper = PerformanceHelperWrapperWrapper()!
  let mainSplitViewController = NSSplitViewController()
  let leftSplitViewController = LeftSplitViewController()
  let searchFieldDelegate = SearchFieldDelegate()

  func applicationDidFinishLaunching(_ aNotification: Notification) {
    Swift.print("Controller.applicationDidFinishLaunching(Notification)")
    
    leftSplitView = NSSplitView(frame: NSRect(x: 0, y: 0, width: 200, height: 600))
    leftSplitView!.dividerStyle = .thin
    leftSplitView!.addSubview(NSView())
    leftSplitView!.addSubview(NSView())
    leftSplitView!.adjustSubviews()
    leftSplitView!.setPosition(474, ofDividerAt: 0)
    leftSplitView!.delegate = leftSplitViewController
    
    objectsScrollView = NSScrollView(frame: leftSplitView!.subviews[0].bounds)
    objectsScrollView!.hasVerticalScroller = true
    objectsScrollView!.hasHorizontalScroller = true
    objectsScrollView!.wantsLayer = true
    objectsScrollView!.identifier = NSUserInterfaceItemIdentifier.init(rawValue: "ObjectsScrollView")
    leftSplitView!.subviews[0] = objectsScrollView!
    
    objectsClipView = NSClipView(frame: leftSplitView!.subviews[0].bounds)
    objectsScrollView!.contentView = objectsClipView!
    
    objectsSourceList = OutlineView(frame: objectsScrollView!.bounds)
    objectsSourceList!.controller = self
    objectsSourceList!.style = .sourceList
    objectsSourceList!.floatsGroupRows = false
    objectsSourceList!.indentationPerLevel = 16
    objectsSourceList!.indentationMarkerFollowsCell = false
    objectsSourceList!.wantsLayer = true
//    objectsSourceList!.layer!.backgroundColor = NSColor.secondarySelectedControlColor.cgColor
    objectsSourceList!.headerView = nil
    objectsSourceList!.allowsMultipleSelection = true
    objectsClipView!.documentView = objectsSourceList!
    
    objectsSourceListColumn = NSTableColumn(identifier: .init("Objects"))
    objectsSourceListColumn!.isEditable = false
    objectsSourceListColumn!.minWidth = 200
    objectsSourceListColumn!.headerCell.stringValue = "Object"
    objectsSourceList!.addTableColumn(objectsSourceListColumn!)
    objectsSourceList!.outlineTableColumn = objectsSourceListColumn
    
    attributesScrollView = NSScrollView(frame: leftSplitView!.subviews[1].bounds)
    attributesScrollView!.hasVerticalScroller = true
    attributesScrollView!.hasHorizontalScroller = true
    attributesScrollView!.wantsLayer = true
    attributesScrollView!.identifier = NSUserInterfaceItemIdentifier.init(rawValue: "AttributesScrollView")
    leftSplitView!.subviews[1] = attributesScrollView!
    
    attributesClipView = NSClipView(frame: leftSplitView!.subviews[1].bounds)
    attributesScrollView!.contentView = attributesClipView!
    
    attributesTableView = NSTableView(frame: attributesScrollView!.bounds)
    attributesClipView!.documentView = attributesTableView!
    
    attributeNamesColumn = NSTableColumn(identifier: .init("A"))
    attributeNamesColumn!.title = "Attribute"
    attributeValuesColumn = NSTableColumn(identifier: .init("V"))
    attributeValuesColumn!.title = "Value"
    attributesTableView!.addTableColumn(attributeNamesColumn!)
    attributesTableView!.addTableColumn(attributeValuesColumn!)
    
    let defaultDevice = MTLCreateSystemDefaultDevice()
    metalView = MetalView(frame: NSRect(x: 0, y: 0, width: 800, height: 600), device: defaultDevice)
    metalView!.controller = self
    metalView!.dataManager = dataManager
    
    let statusBarHeight: CGFloat = 36
    let barInset: CGFloat = 8
    let initialBarWidth = metalView!.bounds.width - barInset * 2
    
    let statusBar = NSVisualEffectView()
    statusBar.material = .hudWindow
    statusBar.blendingMode = .withinWindow
    statusBar.state = .followsWindowActiveState
    statusBar.wantsLayer = true
    statusBar.layer?.cornerRadius = 8
    statusBar.layer?.masksToBounds = true
    statusBar.frame = NSRect(x: barInset, y: barInset, width: initialBarWidth, height: statusBarHeight)
    statusBar.autoresizingMask = [.width, .minYMargin]
    statusBar.isHidden = true
    metalView!.addSubview(statusBar)
    statusBarView = statusBar
    
    progressIndicator = NSProgressIndicator()
    progressIndicator!.isIndeterminate = false
    progressIndicator!.frame = NSRect(x: 8, y: (statusBarHeight - 12) / 2, width: initialBarWidth - 196, height: 12)
    progressIndicator!.autoresizingMask = [.width, .maxXMargin]
    statusBar.addSubview(progressIndicator!)
    
    statusTextField = NSTextField()
    statusTextField!.stringValue = "Ready"
    statusTextField!.isBordered = false
    statusTextField!.isEditable = false
    statusTextField!.drawsBackground = false
    statusTextField!.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
    statusTextField!.textColor = .secondaryLabelColor
    statusTextField!.frame = NSRect(x: initialBarWidth - 188, y: (statusBarHeight - 14) / 2, width: 180, height: 14)
    statusTextField!.autoresizingMask = [.minXMargin]
    statusBar.addSubview(statusTextField!)
    
    dataManager.controller = self
    
    // NSSplitViewController for the main horizontal split
    let sidebarVC = NSViewController()
    let sidebarEffectView = NSVisualEffectView()
    sidebarEffectView.material = .sidebar
    sidebarEffectView.blendingMode = .behindWindow
    sidebarEffectView.state = .followsWindowActiveState
    sidebarEffectView.addSubview(leftSplitView!)
    leftSplitView!.frame = sidebarEffectView.bounds
    leftSplitView!.autoresizingMask = [.width, .height]
    sidebarVC.view = sidebarEffectView
    
    let contentVC = NSViewController()
    contentVC.view = metalView!
    
    let sidebarItem = NSSplitViewItem(sidebarWithViewController: sidebarVC)
    sidebarItem.minimumThickness = 200
    mainSplitViewController.addSplitViewItem(sidebarItem)
    mainSplitViewController.addSplitViewItem(NSSplitViewItem(viewController: contentVC))
    
    splitView = mainSplitViewController.splitView
    splitView!.autoresizingMask = [.width, .height]
    
    window.contentView!.addSubview(mainSplitViewController.view)
    mainSplitViewController.view.frame = window.contentView!.bounds
    mainSplitViewController.view.autoresizingMask = [.width, .height]
    window.makeFirstResponder(metalView)
    toggleViewEdgesMenuItem.state = .on
    window.minSize = NSSize(width: 400, height: 300)
    
    // Unified toolbar with integrated title
    window.styleMask.insert(.unifiedTitleAndToolbar)
    window.toolbarStyle = .unified
    
    let toolbar = NSToolbar(identifier: "azul.toolbar")
    toolbar.delegate = self
    toolbar.allowsUserCustomization = false
    toolbar.autosavesConfiguration = false
    window.toolbar = toolbar
    
    objectsSourceList!.dataSource = dataManager
    objectsSourceList!.delegate = dataManager
    objectsSourceList!.doubleAction = #selector(sourceListDoubleClick)
    attributesTableView!.dataSource = dataManager
    attributesTableView!.delegate = dataManager
  }
  
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return true
  }
  
  // MARK: - NSToolbarDelegate
  
  func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
    return [.lodSelector, .flexibleSpace, .search]
  }
  
  func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
    return [.lodSelector, .search]
  }
  
  func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
    switch itemIdentifier {
    case .lodSelector:
      let lodItem = NSToolbarItem(itemIdentifier: itemIdentifier)
      let seg = NSSegmentedControl()
      seg.segmentCount = 1
      seg.setLabel("0", forSegment: 0)
      seg.selectedSegment = 0
      seg.target = self
      seg.action = #selector(lodSegmentChanged)
      seg.segmentStyle = .separated
      lodItem.view = seg
      lodSegmentedControl = seg
      return lodItem
      
    case .search:
      let searchItem = NSSearchToolbarItem(itemIdentifier: itemIdentifier)
      searchItem.searchField.delegate = searchFieldDelegate
      searchFieldDelegate.controller = self
      searchField = searchItem.searchField
      return searchItem
      
    default:
      return nil
    }
  }
  
  @IBAction func new(_ sender: NSMenuItem) {
    Swift.print("Controller.new(NSMenuItem)")
    dataManager.clear()
    regenerateBoundingBoxBuffer()
    metalView!.new()
    objectsSourceList!.reloadData()
    attributesTableView!.reloadData()
    openFiles = Set<URL>()
    self.window.representedURL = nil
    self.window.title = "azul"
    self.statusBarView?.isHidden = true
    self.updateLodSegments()
  }
  
  @IBAction func openFile(_ sender: NSMenuItem) {
    Swift.print("Controller.openFile(NSMenuItem)")
    
    let openPanel = NSOpenPanel()
    openPanel.allowsMultipleSelection = true
    openPanel.canChooseDirectories = false
    openPanel.canChooseFiles = true
    openPanel.allowedContentTypes = [UTType(filenameExtension: "gml")!, UTType(filenameExtension: "xml")!, UTType(filenameExtension: "json")!, UTType(filenameExtension: "jsonl")!, UTType(filenameExtension: "obj")!, UTType(filenameExtension: "off")!, UTType(filenameExtension: "poly")!]
    
    openPanel.beginSheetModal(for: window) { (result: NSApplication.ModalResponse) in
      if result == .OK {
        self.loadData(from: openPanel.urls)
      }
    }
  }
  
  func application(_ sender: NSApplication, openFile filename: String) -> Bool {
    Swift.print("Controller.application(NSApplication, openFile: String)")
    Swift.print("Open \(filename)")
    let url = URL(fileURLWithPath: filename)
    loadData(from: [url])
    return true
  }
  
  func application(_ sender: NSApplication, openFiles filenames: [String]) {
    Swift.print("Controller.application(NSApplication, openFiles: String)")
    Swift.print("Open \(filenames)")
    var urls = [URL]()
    for filename in filenames {
      urls.append(URL(fileURLWithPath: filename))
    }
    loadData(from: urls)
  }
  
  @IBAction func toggleViewEdges(_ sender: NSMenuItem) {
    if metalView!.viewEdges {
      metalView!.viewEdges = false
      sender.state = .off
    } else {
      metalView!.viewEdges = true
      sender.state = .on
    }
    metalView!.needsDisplay = true
  }
  
  @IBAction func toggleViewBoundingBox(_ sender: NSMenuItem) {
    if metalView!.viewBoundingBox {
      metalView!.viewBoundingBox = false
      sender.state = .off
    } else {
      metalView!.viewBoundingBox = true
      sender.state = .on
    }
    metalView!.needsDisplay = true
  }
  
  @IBAction func goHome(_ sender: NSMenuItem) {
    metalView!.goHome()
  }
  
  @IBAction func toggleSideBar(_ sender: NSMenuItem) {
    NSAnimationContext.runAnimationGroup { context in
      context.allowsImplicitAnimation = true
      mainSplitViewController.toggleSidebar(sender)
    } completionHandler: {
      sender.title = self.mainSplitViewController.splitViewItems[0].isCollapsed ? "Show Sidebar" : "Hide Sidebar"
    }
  }
  
  @IBAction func focusOnSearchBar(_ sender: NSMenuItem) {
    guard let searchField = searchField else { return }
    window.makeFirstResponder(searchField)
  }
  
  func loadData(from urls: [URL]) {
    self.performanceHelper.startTimer()
    
    let progressPerFile = 100.0/Double(urls.count)
    totalProgress = 0.0
    progressIndicator?.doubleValue = totalProgress
    statusBarView?.isHidden = false
    DispatchQueue.global().async(qos: .userInitiated) {
      for url in urls {
        
        if url.pathExtension == "azulview" {
          Swift.print("View url: \(url)")
          DispatchQueue.main.async {
            self.loadViewParameters(url: url)
            self.totalProgress += progressPerFile
            self.progressIndicator?.doubleValue = self.totalProgress
            if urls.last == url {
              self.statusBarView?.isHidden = true
            }
          }
          continue
        }
        
        if self.openFiles.contains(url) {
          Swift.print("\(url) already open")
          DispatchQueue.main.async {
            self.totalProgress += progressPerFile
            self.progressIndicator?.doubleValue = self.totalProgress
            if urls.last == url {
              self.statusBarView?.isHidden = true
            }
          }
          continue
        }
        
        Swift.print("Loading " + url.path + "...")
        DispatchQueue.main.async {
          self.statusBarView?.isHidden = false
          self.statusTextField?.stringValue = "Loading " + url.path + "..."
        }
        url.path.utf8CString.withUnsafeBufferPointer { pointer in
          self.dataManager.parse(pointer.baseAddress)
        }
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*20.071734/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Clearing helpers...")
        self.dataManager.clearHelpers()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*0.51605/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Updating bounds...")
        self.dataManager.updateBoundsWithLastFile()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*0.158675/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Triangulating...")
        self.dataManager.triangulateLastFile()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*45.400172/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Generating edges...")
        self.dataManager.generateEdgesForLastFile()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*1.150533/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Clearing polygons...")
        self.dataManager.clearPolygonsOfLastFile()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*0.359982/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Making triangle buffers...")
        self.dataManager.regenerateTriangleBuffers(withMaximumSize: 16*1024*1024)
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*3.535023/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Making edge buffers...")
        self.dataManager.regenerateEdgeBuffers(withMaximumSize: 16*1024*1024)
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*2.085606/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Loading triangle buffers...")
        while self.metalView == nil {
          Thread.sleep(forTimeInterval: 0.01)
        }
        self.reloadTriangleBuffers()
        self.updateSelectionStateBuffer()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*1.31523/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Loading edge buffers...")
        self.reloadEdgeBuffers()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*0.572072/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        Swift.print("Regenerating bounding box buffer...")
        self.regenerateBoundingBoxBuffer()
        self.performanceHelper.printTimeSpent()
        self.performanceHelper.printMemoryUsage()
        DispatchQueue.main.async {
          self.totalProgress += progressPerFile*0.000162/75.165239
          self.progressIndicator?.doubleValue = self.totalProgress
          self.statusBarView?.isHidden = false
        }
        
        self.openFiles.insert(url)
        NSDocumentController.shared.noteNewRecentDocumentURL(url)
        
        DispatchQueue.main.async {
          self.metalView!.needsDisplay = true
          self.objectsSourceList!.reloadData()
          for row in 0..<self.objectsSourceList!.numberOfRows {
            if let item = self.objectsSourceList!.item(atRow: row), self.objectsSourceList!.parent(forItem: item) == nil {
              self.objectsSourceList!.expandItem(item)
            }
          }
          switch self.openFiles.count {
          case 0:
            self.window.representedURL = nil
            self.window.title = "azul"
          case 1:
            self.window.representedURL = self.openFiles.first!
            self.window.title = self.openFiles.first!.lastPathComponent
          default:
            self.window.representedURL = nil
            self.window.title = "azul (\(self.openFiles.count) open files)"
          }
          if urls.last == url {
            Swift.print("status message: \(self.dataManager.statusMessage()!)")
            self.statusTextField?.stringValue = self.dataManager.statusMessage()
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
              self.statusBarView?.isHidden = true
            }
            self.updateLodSegments()
          }
        }
      }
    }
  }
  
  func regenerateBoundingBoxBuffer() {
    
    // Get bounds
    let firstMinCoordinate = dataManager.minCoordinates()
    let minCoordinatesBuffer = UnsafeBufferPointer(start: firstMinCoordinate, count: 3)
    let minCoordinatesArray = ContiguousArray(minCoordinatesBuffer)
    let minCoordinates = [Float](minCoordinatesArray)
    let firstMidCoordinate = dataManager.midCoordinates()
    let midCoordinatesBuffer = UnsafeBufferPointer(start: firstMidCoordinate, count: 3)
    let midCoordinatesArray = ContiguousArray(midCoordinatesBuffer)
    let midCoordinates = [Float](midCoordinatesArray)
    let firstMaxCoordinate = dataManager.maxCoordinates()
    let maxCoordinatesBuffer = UnsafeBufferPointer(start: firstMaxCoordinate, count: 3)
    let maxCoordinatesArray = ContiguousArray(maxCoordinatesBuffer)
    let maxCoordinates = [Float](maxCoordinatesArray)
    let maxRange = dataManager.maxRange()
    
    // Create bounding box vertices
    let boundingBoxVertices: [Vertex] = [Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 000 -> 001
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 000 -> 010
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 000 -> 100
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),  // 001 -> 011
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),  // 001 -> 101
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 010 -> 011
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 010 -> 110
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((minCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),  // 011 -> 111
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 100 -> 101
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 100 -> 110
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (minCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),  // 101 -> 111
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange)),
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (minCoordinates[2]-midCoordinates[2])/maxRange)),  // 110 -> 111
                                         Vertex(position: SIMD3<Float>((maxCoordinates[0]-midCoordinates[0])/maxRange,
                                                                       (maxCoordinates[1]-midCoordinates[1])/maxRange,
                                                                       (maxCoordinates[2]-midCoordinates[2])/maxRange))]
    metalView!.boundingBoxBuffer = metalView!.device!.makeBuffer(bytes: boundingBoxVertices, length: MemoryLayout<Vertex>.size*boundingBoxVertices.count, options: [])
  }
  
  @objc func reloadTriangleBuffers() {
    self.metalView!.triangleBuffers.removeAll()
    self.dataManager.initialiseTriangleBufferIterator()
    while !self.dataManager.triangleBufferIteratorEnded() {
      var bufferTypeLength: Int = 0
      let firstCharacterOfBufferType = UnsafeRawPointer(self.dataManager.currentTriangleBufferType(withLength: &bufferTypeLength))
      let bufferTypeData = Data(bytes: firstCharacterOfBufferType!, count: bufferTypeLength*MemoryLayout<Int8>.size)
      let bufferType = String(data: bufferTypeData, encoding: .utf8)!
      
      let firstBufferColourComponent = self.dataManager.currentTriangleBufferColour()
      let bufferColourBuffer = UnsafeBufferPointer(start: firstBufferColourComponent, count: 4)
      let bufferColourArray = ContiguousArray(bufferColourBuffer)
      let bufferColour = SIMD4<Float>(bufferColourArray[0], bufferColourArray[1], bufferColourArray[2], bufferColourArray[3])
      
      var vertexBufferSize: Int = 0
      let vertexBuffer = self.dataManager.currentTriangleBuffer(withSize: &vertexBufferSize)
      
      var indexBufferSize: Int = 0
      let indexBuffer = self.dataManager.currentTriangleBufferIndices(withSize: &indexBufferSize)
      
      if vertexBuffer != nil && indexBuffer != nil && indexBufferSize > 0 {
        let vertexMTLBuffer = self.metalView!.device!.makeBuffer(bytes: vertexBuffer!, length: vertexBufferSize, options: [])!
        let indexMTLBuffer = self.metalView!.device!.makeBuffer(bytes: indexBuffer!, length: indexBufferSize, options: [])!
        self.metalView!.triangleBuffers.append(BufferWithColour(buffer: vertexMTLBuffer,
                                                                indexBuffer: indexMTLBuffer,
                                                                indexCount: indexBufferSize / MemoryLayout<UInt32>.size,
                                                                type: bufferType,
                                                                colour: bufferColour))
      }
      self.dataManager.advanceTriangleBufferIterator()
    }
  }
  
  @objc func reloadEdgeBuffers() {
    self.metalView!.edgeBuffers.removeAll()
    self.dataManager.initialiseEdgeBufferIterator()
    while !self.dataManager.edgeBufferIteratorEnded() {
      let firstBufferColourComponent = self.dataManager.currentEdgeBufferColour()
      let bufferColourBuffer = UnsafeBufferPointer(start: firstBufferColourComponent, count: 4)
      let bufferColourArray = ContiguousArray(bufferColourBuffer)
      let bufferColour = SIMD4<Float>(bufferColourArray[0], bufferColourArray[1], bufferColourArray[2], bufferColourArray[3])
      
      var bufferSize: Int = 0
      let buffer = self.dataManager.currentEdgeBuffer(withSize: &bufferSize)
      if buffer != nil {
        let vertexMTLBuffer = self.metalView!.device!.makeBuffer(bytes: buffer!, length: bufferSize, options: [])!
        self.metalView!.edgeBuffers.append(BufferWithColour(buffer: vertexMTLBuffer,
                                                            indexBuffer: vertexMTLBuffer,
                                                            indexCount: 0,
                                                            type: "",
                                                            colour: bufferColour))
      }
      self.dataManager.advanceEdgeBufferIterator()
    }
  }

  @objc func updateSelectionStateBuffer() {
    let count = Int(dataManager.selectionStateCount())
    guard count > 0 else { return }
    guard let ptr = dataManager.selectionStateData() else { return }
    let data = Data(bytes: UnsafeRawPointer(ptr), count: count * MemoryLayout<Float>.size)
    metalView!.updateSelectionStateBuffer(data)
  }

  func applicationWillTerminate(_ aNotification: Notification) {
    Swift.print("Controller.applicationWillTerminate(Notification)")
  }
  
  @objc func sourceListDoubleClick(_ sender: Any?) {
    dataManager.sourceListDoubleClick()
    
    // Put model matrix in arrays and render
    metalView!.constants.modelMatrix = metalView!.modelMatrix
    metalView!.constants.modelViewProjectionMatrix = matrix_multiply(metalView!.projectionMatrix, matrix_multiply(metalView!.viewMatrix, metalView!.modelMatrix))
    metalView!.constants.modelMatrixInverseTransposed = matrix_upper_left_3x3(matrix: metalView!.modelMatrix).inverse.transpose
    metalView!.needsDisplay = true
  }
  
  @objc func lodSegmentChanged(_ sender: NSSegmentedControl) {
    if sender.selectedSegment == sender.segmentCount - 1 {
      "__highest__".withCString { pointer in
        dataManager.setLodFilter(pointer)
      }
    } else {
      guard let lodValue = sender.label(forSegment: sender.selectedSegment) else { return }
      lodValue.withCString { pointer in
        dataManager.setLodFilter(pointer)
      }
    }
    dataManager.regenerateTriangleBuffers(withMaximumSize: 16*1024*1024)
    self.reloadTriangleBuffers()
    self.updateSelectionStateBuffer()
    dataManager.regenerateEdgeBuffers(withMaximumSize: 16*1024*1024)
    self.reloadEdgeBuffers()
    metalView!.needsDisplay = true
    objectsSourceList!.reloadData()
  }
  
  func updateLodSegments() {
    let lods = dataManager.availableLods() ?? []
    guard let control = lodSegmentedControl else { return }
    
    if lods.isEmpty {
      control.isHidden = true
      return
    }
    
    control.isHidden = false
    let sortedLods = lods.sorted()
    control.segmentCount = 1 + sortedLods.count
    for (index, lod) in sortedLods.enumerated() {
      control.setLabel("\(lod)", forSegment: index)
    }
    control.setLabel("Highest", forSegment: sortedLods.count)
    control.selectedSegment = sortedLods.count
    "__highest__".withCString { pointer in
      dataManager.setLodFilter(pointer)
    }
    
    dataManager.regenerateTriangleBuffers(withMaximumSize: 16*1024*1024)
    reloadTriangleBuffers()
    updateSelectionStateBuffer()
    dataManager.regenerateEdgeBuffers(withMaximumSize: 16*1024*1024)
    reloadEdgeBuffers()
    metalView!.needsDisplay = true
    objectsSourceList!.reloadData()
    for row in 0..<objectsSourceList!.numberOfRows {
      if let item = objectsSourceList!.item(atRow: row), objectsSourceList!.parent(forItem: item) == nil {
        objectsSourceList!.expandItem(item)
      }
    }
  }
  
  @IBAction func copyObjectId(_ sender: NSMenuItem) {
    let pasteboard = NSPasteboard.general
    pasteboard.clearContents()
    pasteboard.declareTypes([NSPasteboard.PasteboardType.string], owner: self)
    var selectionString = String()
    for row in objectsSourceList!.selectedRowIndexes {
      if let item = objectsSourceList!.item(atRow: row) {
        if let objectId = dataManager.objectId(forItem: item), !objectId.isEmpty {
          if !selectionString.isEmpty {
            selectionString.append("\n")
          }
          selectionString.append(objectId)
        }
      }
    }
    pasteboard.setString(selectionString, forType: NSPasteboard.PasteboardType.string)
  }
  
  func loadViewParameters(url: URL) {
    do {
      let jsonDecoder = JSONDecoder()
      let jsonData = try Data(contentsOf: url)
      let viewParameters = try jsonDecoder.decode(ViewParameters.self, from: jsonData)
      self.metalView!.eye = deserialiseToFloat3(vector: viewParameters.eye)
      self.metalView!.centre = deserialiseToFloat3(vector: viewParameters.centre)
      self.metalView!.fieldOfView = viewParameters.fieldOfView
      self.metalView!.modelTranslationToCentreOfRotationMatrix = deserialiseToMatrix4x4(matrix: viewParameters.modelTranslationToCentreOfRotationMatrix)
      self.metalView!.modelRotationMatrix = deserialiseToMatrix4x4(matrix: viewParameters.modelRotationMatrix)
      self.metalView!.modelShiftBackMatrix = deserialiseToMatrix4x4(matrix: viewParameters.modelShiftBackMatrix)
      self.metalView!.modelMatrix = deserialiseToMatrix4x4(matrix: viewParameters.modelMatrix)
      self.metalView!.viewMatrix = deserialiseToMatrix4x4(matrix: viewParameters.viewMatrix)
      self.metalView!.projectionMatrix = deserialiseToMatrix4x4(matrix: viewParameters.projectionMatrix)
      self.metalView!.viewEdges = viewParameters.viewEdges
      self.metalView!.viewBoundingBox = viewParameters.viewBoundingBox
      
      self.metalView!.constants.modelMatrix = self.metalView!.modelMatrix
      self.metalView!.constants.modelViewProjectionMatrix = matrix_multiply(self.metalView!.projectionMatrix, matrix_multiply(self.metalView!.viewMatrix, self.metalView!.modelMatrix))
      self.metalView!.constants.modelMatrixInverseTransposed = matrix_upper_left_3x3(matrix: self.metalView!.modelMatrix).inverse.transpose
      self.metalView!.needsDisplay = true
    } catch {
      Swift.print("Couldn't load view parameters...")
    }
  }
  
  @IBAction func loadViewParameters(_ sender: NSMenuItem) {
    let openPanel = NSOpenPanel()
    openPanel.allowsMultipleSelection = false
    openPanel.canChooseDirectories = false
    openPanel.canChooseFiles = true
    openPanel.allowedContentTypes = [UTType(filenameExtension: "azulview")!]
    openPanel.beginSheetModal(for: window) { (result: NSApplication.ModalResponse) in
      if result == .OK {
        self.loadViewParameters(url: openPanel.url!)
      }
    }
  }
  
  @IBAction func saveViewParameters(_ sender: NSMenuItem) {
    let jsonEncoder = JSONEncoder()
    let viewParameters = ViewParameters(eye: serialise(vector: metalView!.eye),
                                        centre: serialise(vector: metalView!.centre),
                                        fieldOfView: metalView!.fieldOfView,
                                        modelTranslationToCentreOfRotationMatrix: serialise(matrix: metalView!.modelTranslationToCentreOfRotationMatrix),
                                        modelRotationMatrix: serialise(matrix: metalView!.modelRotationMatrix),
                                        modelShiftBackMatrix: serialise(matrix: metalView!.modelShiftBackMatrix),
                                        modelMatrix: serialise(matrix: metalView!.modelMatrix),
                                        viewMatrix: serialise(matrix: metalView!.viewMatrix),
                                        projectionMatrix: serialise(matrix: metalView!.projectionMatrix),
                                        viewEdges: metalView!.viewEdges,
                                        viewBoundingBox: metalView!.viewBoundingBox)
    do {
      let jsonData = try jsonEncoder.encode(viewParameters)
      let savePanel = NSSavePanel()
      savePanel.allowedContentTypes = [UTType(filenameExtension: "azulview")!]
      savePanel.beginSheetModal(for: window, completionHandler: { (result: NSApplication.ModalResponse) in
        if result == .OK {
          do {
            try jsonData.write(to: savePanel.url!, options: [])
          } catch {
            Swift.print("Couldn't write file...")
          }
        }
      })
    } catch {
      Swift.print("Couldn't encode...")
    }
  }
}
