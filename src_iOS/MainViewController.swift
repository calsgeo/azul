import UIKit
import Metal
import MetalKit
import UniformTypeIdentifiers

class MainViewController: UIViewController, MTKViewDelegate {

    // MARK: Data manager
    lazy var dataManager: DataManagerWrapperWrapper = DataManagerWrapperWrapper()!

    // MARK: Metal
    var metalView: MTKView!
    var device: MTLDevice!
    var commandQueue: MTLCommandQueue!

    // MARK: Camera
    var cameraPosition = simd_float3(0, 0, 100)
    var cameraTarget = simd_float3(0, 0, 0)
    var cameraUp = simd_float3(0, 1, 0)
    var fieldOfView: Float = 45.0
    var nearZ: Float = 0.1
    var farZ: Float = 10000.0

    // MARK: Gesture state
    var lastPanLocation: CGPoint?
    var lastPanTranslation: CGPoint = .zero
    var currentPinchScale: CGFloat = 1.0

    // MARK: Scene state
    var showingEdges = true
    var showingBBox = true

    // MARK: Floating buttons
    var openButton: UIButton!
    var objectsButton: UIButton!
    var edgesButton: UIButton!
    var bboxButton: UIButton!
    var homeButton: UIButton!

    // MARK: Lifecycle
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        dataManager.controller = self
        setupMetal()
        setupFloatingButtons()
        if metalView != nil {
            setupGestures()
        }
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        metalView?.frame = view.bounds
    }

    // MARK: Metal setup
    func setupMetal() {
        device = MTLCreateSystemDefaultDevice()
        guard device != nil else {
            let label = UILabel()
            label.text = "Metal not available on simulator.\nRun on a real device to see the 3D view."
            label.textAlignment = .center
            label.numberOfLines = 0
            label.textColor = .white
            label.font = .systemFont(ofSize: 14)
            label.translatesAutoresizingMaskIntoConstraints = false
            view.addSubview(label)
            NSLayoutConstraint.activate([
                label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
                label.centerYAnchor.constraint(equalTo: view.centerYAnchor),
                label.leadingAnchor.constraint(greaterThanOrEqualTo: view.leadingAnchor, constant: 40),
                label.trailingAnchor.constraint(lessThanOrEqualTo: view.trailingAnchor, constant: -40),
            ])
            return
        }
        commandQueue = device.makeCommandQueue()

        metalView = MTKView(frame: view.bounds, device: device)
        metalView.delegate = self
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        metalView.clearColor = MTLClearColorMake(0.15, 0.15, 0.20, 1.0)
        metalView.depthStencilPixelFormat = .depth32Float
        metalView.sampleCount = 4
        metalView.colorPixelFormat = .bgra8Unorm
        metalView.autoresizesSubviews = true
        view.addSubview(metalView)
        view.sendSubviewToBack(metalView)
    }

    func updateCamera() {
        guard metalView != nil else { return }
        let aspect = Float(metalView.drawableSize.width / metalView.drawableSize.height)
        var projection = matrix4x4_perspective(fieldOfView: fieldOfView * .pi / 180.0,
                                                aspectRatio: aspect,
                                                nearZ: nearZ,
                                                farZ: farZ)
        let viewMatrix = matrix4x4_look_at(eye: cameraPosition, centre: cameraTarget, up: cameraUp)
        let modelMatrix = matrix_identity_float4x4
        _ = projection * viewMatrix * modelMatrix
    }

    // MARK: MTKViewDelegate
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        updateCamera()
    }

    func draw(in view: MTKView) {
        guard let commandBuffer = commandQueue.makeCommandBuffer() else { return }
        guard let renderPassDescriptor = view.currentRenderPassDescriptor else { return }
        guard let drawable = view.currentDrawable else { return }

        renderPassDescriptor.colorAttachments[0].clearColor = metalView.clearColor
        renderPassDescriptor.colorAttachments[0].loadAction = .clear
        renderPassDescriptor.colorAttachments[0].storeAction = .store

        guard let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor) else {
            return
        }
        encoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    // MARK: Floating buttons
    func setupFloatingButtons() {
        let buttonConfig = UIImage.SymbolConfiguration(pointSize: 18, weight: .medium)

        openButton = makeFloatingButton(systemName: "folder.fill", config: buttonConfig, action: #selector(openFile))
        objectsButton = makeFloatingButton(systemName: "list.bullet", config: buttonConfig, action: #selector(showObjects))
        edgesButton = makeFloatingButton(systemName: "square.dashed", config: buttonConfig, action: #selector(toggleEdges))
        bboxButton = makeFloatingButton(systemName: "rectangle.center.inset.filled", config: buttonConfig, action: #selector(toggleBBox))
        homeButton = makeFloatingButton(systemName: "house.fill", config: buttonConfig, action: #selector(goHome))

        let topStack = UIStackView(arrangedSubviews: [openButton])
        topStack.spacing = 8
        topStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(topStack)

        let bottomStack = UIStackView(arrangedSubviews: [objectsButton, edgesButton, bboxButton, homeButton])
        bottomStack.axis = .horizontal
        bottomStack.spacing = 12
        bottomStack.distribution = .equalSpacing
        bottomStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(bottomStack)

        NSLayoutConstraint.activate([
            topStack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 8),
            topStack.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 12),

            bottomStack.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -12),
            bottomStack.centerXAnchor.constraint(equalTo: view.centerXAnchor),
        ])
    }

    func makeFloatingButton(systemName: String, config: UIImage.SymbolConfiguration, action: Selector) -> UIButton {
        let image = UIImage(systemName: systemName, withConfiguration: config)
        let button = UIButton(type: .system)
        button.setImage(image, for: .normal)
        button.tintColor = .white
        button.backgroundColor = UIColor(white: 0, alpha: 0.4)
        button.layer.cornerRadius = 20
        button.layer.cornerCurve = .continuous
        button.translatesAutoresizingMaskIntoConstraints = false
        button.widthAnchor.constraint(equalToConstant: 40).isActive = true
        button.heightAnchor.constraint(equalToConstant: 40).isActive = true
        button.addTarget(self, action: action, for: .touchUpInside)
        return button
    }

    // MARK: Gesture recognizers
    func setupGestures() {
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.minimumNumberOfTouches = 1
        pan.maximumNumberOfTouches = 1
        metalView.addGestureRecognizer(pan)

        let twoFingerPan = UIPanGestureRecognizer(target: self, action: #selector(handleTwoFingerPan(_:)))
        twoFingerPan.minimumNumberOfTouches = 2
        metalView.addGestureRecognizer(twoFingerPan)

        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        metalView.addGestureRecognizer(pinch)

        let rotation = UIRotationGestureRecognizer(target: self, action: #selector(handleRotation(_:)))
        metalView.addGestureRecognizer(rotation)

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        metalView.addGestureRecognizer(tap)
    }

    @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
        let translation = gesture.translation(in: metalView)
        let delta = CGPoint(x: translation.x - lastPanTranslation.x,
                           y: translation.y - lastPanTranslation.y)
        lastPanTranslation = translation

        if gesture.state == .began {
            lastPanTranslation = .zero
        } else if gesture.state == .changed {
            let sensitivity: Float = 0.005
            let angleX = Float(delta.y) * sensitivity
            let angleY = Float(delta.x) * sensitivity
            let forward = normalize(cameraTarget - cameraPosition)
            let right = normalize(cross(forward, cameraUp))
            let up = cameraUp

            let rotationX = matrix4x4_rotation(angle: angleX, axis: right)
            let rotationY = matrix4x4_rotation(angle: angleY, axis: up)
            let rotatedForward = (rotationY * rotationX) * simd_float4(forward.x, forward.y, forward.z, 0)
            let distance = simd_length(cameraTarget - cameraPosition)
            cameraPosition = cameraTarget - simd_float3(rotatedForward.x, rotatedForward.y, rotatedForward.z) * distance
        } else if gesture.state == .ended || gesture.state == .cancelled {
            lastPanTranslation = .zero
        }
    }

    @objc func handleTwoFingerPan(_ gesture: UIPanGestureRecognizer) {
        let translation = gesture.translation(in: metalView)
        if gesture.state == .changed {
            let forward = normalize(cameraTarget - cameraPosition)
            let right = normalize(cross(forward, cameraUp))
            let up = cameraUp
            let sensitivity: Float = 0.05
            let shift = right * Float(-translation.x) * sensitivity + up * Float(translation.y) * sensitivity
            cameraPosition += shift
            cameraTarget += shift
        }
        gesture.setTranslation(.zero, in: metalView)
    }

    @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        if gesture.state == .changed {
            let forward = cameraTarget - cameraPosition
            let distance = simd_length(forward)
            let newDistance = distance / Float(gesture.scale)
            let clampedDistance = max(min(newDistance, farZ * 0.9), nearZ * 2)
            cameraPosition = cameraTarget - normalize(forward) * clampedDistance
            gesture.scale = 1.0
        }
    }

    @objc func handleRotation(_ gesture: UIRotationGestureRecognizer) {
        if gesture.state == .changed {
            let forward = normalize(cameraTarget - cameraPosition)
            let rotation = matrix4x4_rotation(angle: Float(gesture.rotation), axis: forward)
            let rotatedUp = rotation * simd_float4(cameraUp.x, cameraUp.y, cameraUp.z, 0)
            cameraUp = simd_float3(rotatedUp.x, rotatedUp.y, rotatedUp.z)
            gesture.rotation = 0
        }
    }

    // MARK: Buffer updates (called from ObjC++ bridge)
    @objc func updateVisibleStateBuffer() {
    }

    @objc func updateSelectionStateBuffer() {
    }

    @objc func handleTap(_ gesture: UITapGestureRecognizer) {
        let location = gesture.location(in: metalView)
        let width = Float(metalView.drawableSize.width)
        let height = Float(metalView.drawableSize.height)

        guard width > 0, height > 0 else { return }

        let aspect = width / height
        let projection = matrix4x4_perspective(fieldOfView: fieldOfView * .pi / 180.0,
                                                aspectRatio: aspect, nearZ: nearZ, farZ: farZ)
        let viewMatrix = matrix4x4_look_at(eye: cameraPosition, centre: cameraTarget, up: cameraUp)
        let viewProjection = projection * viewMatrix
        let invViewProjection = simd_inverse(viewProjection)

        let x = (2.0 * Float(location.x) / width - 1.0)
        let y = (1.0 - 2.0 * Float(location.y) / height)
        let nearPoint = invViewProjection * simd_float4(x, y, 0, 1)
        let farPoint = invViewProjection * simd_float4(x, y, 1, 1)
        let near = simd_float3(nearPoint.x, nearPoint.y, nearPoint.z) / nearPoint.w
        let far = simd_float3(farPoint.x, farPoint.y, farPoint.z) / farPoint.w

        _ = far - near
    }

    // MARK: Actions
    @objc func openFile() {
        let types: [UTType] = [.json, .xml, UTType(filenameExtension: "obj")!, UTType(filenameExtension: "off")!, UTType(filenameExtension: "poly")!, UTType(filenameExtension: "gml")!, UTType(filenameExtension: "azulview")!].compactMap { $0 }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = true
        if let scene = view.window?.windowScene,
           let rootVC = scene.windows.first?.rootViewController {
            rootVC.present(picker, animated: true)
        } else {
            present(picker, animated: true)
        }
    }

    func loadFile(url: URL) {
        let path = url.path
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            self.dataManager.parse(path)
            self.dataManager.clearHelpers()
            self.dataManager.updateBoundsWithLastFile()
            self.dataManager.triangulateLastFile()
            self.dataManager.generateEdgesForLastFile()
            self.dataManager.clearPolygonsOfLastFile()
            self.dataManager.regenerateTriangleBuffers(withMaximumSize: 16 * 1024 * 1024)
            self.dataManager.regenerateEdgeBuffers(withMaximumSize: 16 * 1024 * 1024)
        }
    }

    @objc func showObjects() {
        let objectsVC = ObjectListViewController()
        objectsVC.dataManager = dataManager
        objectsVC.delegate = self
        objectsVC.title = "Objects"

        let nav = UINavigationController(rootViewController: objectsVC)
        nav.modalPresentationStyle = UIDevice.current.userInterfaceIdiom == .pad ? .popover : .pageSheet
        if let popover = nav.popoverPresentationController {
            popover.sourceView = objectsButton
            popover.permittedArrowDirections = .down
        }
        present(nav, animated: true)
    }

    @objc func toggleEdges() {
        showingEdges.toggle()
        edgesButton.tintColor = showingEdges ? .white : .systemGray
    }

    @objc func toggleBBox() {
        showingBBox.toggle()
        bboxButton.tintColor = showingBBox ? .white : .systemGray
    }

    @objc func goHome() {
        cameraPosition = simd_float3(0, 0, 100)
        cameraTarget = simd_float3(0, 0, 0)
        cameraUp = simd_float3(0, 1, 0)
        fieldOfView = 45.0
        updateCamera()
    }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
}

// MARK: ObjectListViewControllerDelegate
extension MainViewController: ObjectListViewControllerDelegate {
    func objectListDidSelectItem(_ item: AzulObjectIterator) {
        let attrsVC = AttributeTableViewController()
        attrsVC.dataManager = dataManager
        let ident = dataManager.identifier(ofItem: item) ?? ""
        attrsVC.title = ident.isEmpty
            ? (dataManager.type(ofItem: item) ?? "")
            : ident
        attrsVC.selectedItem = item
        attrsVC.tableView.reloadData()

        if let nav = presentedViewController as? UINavigationController {
            nav.pushViewController(attrsVC, animated: true)
        }
    }
}

// MARK: UIDocumentPickerDelegate
extension MainViewController: UIDocumentPickerDelegate {
    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        for url in urls {
            loadFile(url: url)
        }
    }
}
