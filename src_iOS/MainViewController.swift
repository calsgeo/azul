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
    var library: MTLLibrary!

    var litRenderPipelineState: MTLRenderPipelineState?
    var unlitRenderPipelineState: MTLRenderPipelineState?
    var edgeRenderPipelineState: MTLRenderPipelineState?
    var pickingRenderPipelineState: MTLRenderPipelineState?
    var depthStencilState: MTLDepthStencilState?

    var msaaTexture: MTLTexture?
    var msaaDepthTexture: MTLTexture?
    var depthTexture: MTLTexture?
    var pickingTexture: MTLTexture?
    var pickingDepthTexture: MTLTexture?

    var triangleBuffers = [BufferWithColour]()
    var edgeBuffers = [BufferWithColour]()
    var boundingBoxBuffer: MTLBuffer?
    var selectionStateBuffer: MTLBuffer?
    var selectionStateCount: Int = 0
    var visibleStateBuffer: MTLBuffer?
    var visibleStateCount: Int = 0

    var constants = Constants()

    // MARK: Camera (matches macOS MetalView)
    var eye = SIMD3<Float>(0.0, 0.0, 0.0)
    var centre = SIMD3<Float>(0.0, 0.0, -1.0)
    var fieldOfView: Float = 1.047197551196598

    var modelTranslationToCentreOfRotationMatrix = matrix_identity_float4x4
    var modelRotationMatrix = matrix_identity_float4x4
    var modelShiftBackMatrix = matrix_identity_float4x4

    var modelMatrix = matrix_identity_float4x4
    var viewMatrix = matrix_identity_float4x4
    var projectionMatrix = matrix_identity_float4x4

    // MARK: Scene state
    var showingBBox = false
    var selectionColour = SIMD4<Float>(1.0, 1.0, 0.0, 1.0)
    let depthFormat = MTLPixelFormat.depth32Float

    // MARK: Floating buttons
    var openButton: UIButton!
    var objectsButton: UIButton!
    var homeButton: UIButton!

    // MARK: Gesture state
    var lastPanTranslation: CGPoint = .zero

    // MARK: Lifecycle
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        dataManager.controller = self

        device = MTLCreateSystemDefaultDevice()
        guard device != nil else {
            showMessage("Metal not available on simulator.\nRun on a real device to see the 3D view.")
            setupFloatingButtons()
            return
        }
        commandQueue = device.makeCommandQueue()
        library = device.makeDefaultLibrary()

        setupMetal()
        setupFloatingButtons()
        setupGestures()
        setupCamera()

        recreatePipelines()
        createPickingTextures()

        metalView.isPaused = false
        metalView.enableSetNeedsDisplay = false
    }

    func showMessage(_ text: String) {
        let label = UILabel()
        label.text = text
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
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        metalView?.frame = view.bounds
    }

    // MARK: Metal setup
    func setupMetal() {
        metalView = MTKView(frame: view.bounds, device: device)
        metalView.delegate = self
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        metalView.clearColor = MTLClearColorMake(0.15, 0.15, 0.20, 1.0)
        metalView.sampleCount = 1
        metalView.colorPixelFormat = .bgra8Unorm
        metalView.autoresizesSubviews = true
        view.addSubview(metalView)
        view.sendSubviewToBack(metalView)

        let depthDesc = MTLDepthStencilDescriptor()
        depthDesc.depthCompareFunction = .less
        depthDesc.isDepthWriteEnabled = true
        depthStencilState = device.makeDepthStencilState(descriptor: depthDesc)

        createDepthTexture(size: metalView.drawableSize)
    }

    func createDepthTexture(size: CGSize) {
        let w = Int(size.width)
        let h = Int(size.height)
        guard w > 0, h > 0 else { return }
        let desc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: depthFormat, width: w, height: h, mipmapped: false)
        desc.storageMode = .private
        desc.usage = .renderTarget
        depthTexture = device.makeTexture(descriptor: desc)
    }

    func setupCamera() {
        modelShiftBackMatrix = matrix4x4_translation(shift: centre)
        modelMatrix = matrix_multiply(matrix_multiply(modelShiftBackMatrix, modelRotationMatrix), modelTranslationToCentreOfRotationMatrix)
        viewMatrix = matrix4x4_look_at(eye: eye, centre: centre, up: SIMD3<Float>(0.0, 1.0, 0.0))
        projectionMatrix = matrix4x4_perspective_shorter_dim(fieldOfView: fieldOfView, width: Float(metalView.bounds.size.width), height: Float(metalView.bounds.size.height), nearZ: 0.001, farZ: 100.0)
        constants.modelMatrix = modelMatrix
        constants.modelViewProjectionMatrix = matrix_multiply(projectionMatrix, matrix_multiply(viewMatrix, modelMatrix))
        constants.modelMatrixInverseTransposed = matrix_upper_left_3x3(matrix: modelMatrix).inverse.transpose
        constants.viewMatrixInverse = viewMatrix.inverse
    }

    // MARK: Pipelines
    func recreatePipelines() {
        guard let library = library else { return }

        let litDesc = MTLRenderPipelineDescriptor()
        litDesc.vertexFunction = library.makeFunction(name: "vertexLit")
        litDesc.fragmentFunction = library.makeFunction(name: "fragmentLit")
        litDesc.colorAttachments[0].pixelFormat = metalView.colorPixelFormat
        litDesc.colorAttachments[0].isBlendingEnabled = true
        litDesc.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
        litDesc.colorAttachments[0].sourceAlphaBlendFactor = .sourceAlpha
        litDesc.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        litDesc.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
        litDesc.depthAttachmentPixelFormat = depthFormat
        litDesc.rasterSampleCount = metalView.sampleCount

        let unlitDesc = MTLRenderPipelineDescriptor()
        unlitDesc.vertexFunction = library.makeFunction(name: "vertexUnlit")
        unlitDesc.fragmentFunction = library.makeFunction(name: "fragmentUnlit")
        unlitDesc.colorAttachments[0].pixelFormat = metalView.colorPixelFormat
        unlitDesc.colorAttachments[0].isBlendingEnabled = false
        unlitDesc.depthAttachmentPixelFormat = depthFormat
        unlitDesc.rasterSampleCount = metalView.sampleCount

        let edgeDesc = MTLRenderPipelineDescriptor()
        edgeDesc.vertexFunction = library.makeFunction(name: "vertexEdge")
        edgeDesc.fragmentFunction = library.makeFunction(name: "fragmentEdge")
        edgeDesc.colorAttachments[0].pixelFormat = metalView.colorPixelFormat
        edgeDesc.colorAttachments[0].isBlendingEnabled = false
        edgeDesc.depthAttachmentPixelFormat = depthFormat
        edgeDesc.rasterSampleCount = metalView.sampleCount

        let pickDesc = MTLRenderPipelineDescriptor()
        pickDesc.vertexFunction = library.makeFunction(name: "vertexPicking")
        pickDesc.fragmentFunction = library.makeFunction(name: "fragmentPicking")
        pickDesc.colorAttachments[0].pixelFormat = .rgba8Unorm
        pickDesc.depthAttachmentPixelFormat = depthFormat
        pickDesc.rasterSampleCount = 1

        do {
            litRenderPipelineState = try device.makeRenderPipelineState(descriptor: litDesc)
            unlitRenderPipelineState = try device.makeRenderPipelineState(descriptor: unlitDesc)
            edgeRenderPipelineState = try device.makeRenderPipelineState(descriptor: edgeDesc)
            pickingRenderPipelineState = try device.makeRenderPipelineState(descriptor: pickDesc)
        } catch {
            print("Pipeline error: \(error)")
        }
    }

    // MARK: MSAA
    func createMSAATextures(size: CGSize) {
        let w = Int(size.width)
        let h = Int(size.height)
        guard w > 0, h > 0 else { return }
        let sampleCount = metalView.sampleCount
        msaaTexture = nil
        msaaDepthTexture = nil
        if sampleCount > 1 {
            let desc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: metalView.colorPixelFormat, width: w, height: h, mipmapped: false)
            desc.textureType = .type2DMultisample
            desc.sampleCount = sampleCount
            desc.usage = .renderTarget
            desc.storageMode = .private
            msaaTexture = device.makeTexture(descriptor: desc)

            let depthDesc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: depthFormat, width: w, height: h, mipmapped: false)
            depthDesc.textureType = .type2DMultisample
            depthDesc.sampleCount = sampleCount
            depthDesc.usage = .renderTarget
            depthDesc.storageMode = .private
            msaaDepthTexture = device.makeTexture(descriptor: depthDesc)

            if msaaTexture == nil || msaaDepthTexture == nil {
                metalView.sampleCount = 1
                msaaTexture = nil
                msaaDepthTexture = nil
                recreatePipelines()
            }
        }
    }

    func createPickingTextures() {
        let w = Int(metalView.drawableSize.width)
        let h = Int(metalView.drawableSize.height)
        guard w > 0, h > 0 else { return }
        let desc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: false)
        desc.usage = [.renderTarget, .shaderRead]
        pickingTexture = device.makeTexture(descriptor: desc)

        let depthDesc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: depthFormat, width: w, height: h, mipmapped: false)
        depthDesc.storageMode = .private
        depthDesc.usage = .renderTarget
        pickingDepthTexture = device.makeTexture(descriptor: depthDesc)
    }

    // MARK: MTKViewDelegate
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        createDepthTexture(size: size)
        createPickingTextures()
        projectionMatrix = matrix4x4_perspective_shorter_dim(fieldOfView: fieldOfView, width: Float(size.width), height: Float(size.height), nearZ: 0.001, farZ: 100.0)
        constants.modelViewProjectionMatrix = matrix_multiply(projectionMatrix, matrix_multiply(viewMatrix, modelMatrix))
    }

    func draw(in view: MTKView) {
        guard let commandBuffer = commandQueue.makeCommandBuffer(),
              let drawable = view.currentDrawable else { return }

        let renderPassDescriptor = MTLRenderPassDescriptor()
        renderPassDescriptor.colorAttachments[0].texture = drawable.texture
        renderPassDescriptor.colorAttachments[0].storeAction = .store
        renderPassDescriptor.depthAttachment.texture = depthTexture
        renderPassDescriptor.depthAttachment.storeAction = .dontCare
        renderPassDescriptor.colorAttachments[0].clearColor = metalView.clearColor
        renderPassDescriptor.colorAttachments[0].loadAction = .clear
        renderPassDescriptor.depthAttachment.texture = depthTexture
        renderPassDescriptor.depthAttachment.loadAction = .clear
        renderPassDescriptor.depthAttachment.storeAction = .dontCare
        renderPassDescriptor.depthAttachment.clearDepth = 1.0

        let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor)!
        encoder.setFrontFacing(.counterClockwise)
        encoder.setDepthStencilState(depthStencilState)
        encoder.setRenderPipelineState(litRenderPipelineState!)

        if let selBuffer = selectionStateBuffer, selectionStateCount > 0 {
            encoder.setFragmentBuffer(selBuffer, offset: 0, index: 2)
        } else {
            var zero: Float = 0
            encoder.setFragmentBytes(&zero, length: MemoryLayout<Float>.size, index: 2)
        }
        if let visBuffer = visibleStateBuffer, visibleStateCount > 0 {
            encoder.setFragmentBuffer(visBuffer, offset: 0, index: 3)
        } else {
            var one: Float = 1
            encoder.setFragmentBytes(&one, length: MemoryLayout<Float>.size, index: 3)
        }

        // Opaque triangles (alpha == 1.0)
        for tb in triangleBuffers where tb.colour.w == 1.0 {
            encoder.setVertexBuffer(tb.buffer, offset: 0, index: 0)
            constants.colour = tb.colour
            encoder.setVertexBytes(&constants, length: MemoryLayout<Constants>.size, index: 1)
            encoder.setFragmentBytes(&constants, length: MemoryLayout<Constants>.size, index: 0)
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: tb.indexCount, indexType: .uint32, indexBuffer: tb.indexBuffer, indexBufferOffset: 0)
        }

        // Transparent triangles (alpha != 1.0)
        for tb in triangleBuffers where tb.colour.w != 1.0 {
            encoder.setVertexBuffer(tb.buffer, offset: 0, index: 0)
            constants.colour = tb.colour
            encoder.setVertexBytes(&constants, length: MemoryLayout<Constants>.size, index: 1)
            encoder.setFragmentBytes(&constants, length: MemoryLayout<Constants>.size, index: 0)
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: tb.indexCount, indexType: .uint32, indexBuffer: tb.indexBuffer, indexBufferOffset: 0)
        }

        // Edges
        encoder.setRenderPipelineState(edgeRenderPipelineState!)
        if let visBuffer = visibleStateBuffer, visibleStateCount > 0 {
            encoder.setFragmentBuffer(visBuffer, offset: 0, index: 2)
        } else {
            var one: Float = 1
            encoder.setFragmentBytes(&one, length: MemoryLayout<Float>.size, index: 2)
        }
        for eb in edgeBuffers {
            encoder.setVertexBuffer(eb.buffer, offset: 0, index: 0)
            constants.colour = eb.colour
            encoder.setVertexBytes(&constants, length: MemoryLayout<Constants>.size, index: 1)
            encoder.setFragmentBytes(&constants, length: MemoryLayout<Constants>.size, index: 0)
            encoder.drawPrimitives(type: .line, vertexStart: 0, vertexCount: eb.buffer.length / MemoryLayout<EdgeVertex>.size)
        }
        encoder.setRenderPipelineState(unlitRenderPipelineState!)

        // Bounding box
        if showingBBox, let bb = boundingBoxBuffer {
            encoder.setVertexBuffer(bb, offset: 0, index: 0)
            constants.colour = SIMD4<Float>(1.0, 1.0, 1.0, 1.0)
            encoder.setVertexBytes(&constants, length: MemoryLayout<Constants>.size, index: 1)
            encoder.drawPrimitives(type: .line, vertexStart: 0, vertexCount: bb.length / MemoryLayout<Vertex>.size)
        }

        encoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    // MARK: Depth at centre
    func depthAtCentre() -> Float {
        guard let mins = dataManager.minCoordinates(),
              let mids = dataManager.midCoordinates(),
              let maxs = dataManager.maxCoordinates() else { return 0.0 }
        let minCoords = [mins[0], mins[1], mins[2]]
        let midCoords = [mids[0], mids[1], mids[2]]
        let maxCoords = [maxs[0], maxs[1], maxs[2]]
        let range = dataManager.maxRange()

        let leftUpPoint = SIMD4<Float>((minCoords[0]-midCoords[0])/range, (maxCoords[1]-midCoords[1])/range, 0.0, 1.0)
        let rightUpPoint = SIMD4<Float>((maxCoords[0]-midCoords[0])/range, (maxCoords[1]-midCoords[1])/range, 0.0, 1.0)
        let centreDownPoint = SIMD4<Float>(0.0, (minCoords[1]-midCoords[1])/range, 0.0, 1.0)

        let mv = matrix_multiply(viewMatrix, modelMatrix)
        let leftUp = matrix_multiply(mv, leftUpPoint)
        let rightUp = matrix_multiply(mv, rightUpPoint)
        let centreDown = matrix_multiply(mv, centreDownPoint)

        let v1 = SIMD3<Float>(leftUp.x - centreDown.x, leftUp.y - centreDown.y, leftUp.z - centreDown.z)
        let v2 = SIMD3<Float>(rightUp.x - centreDown.x, rightUp.y - centreDown.y, rightUp.z - centreDown.z)
        let cp = cross(v1, v2)
        let p3 = SIMD3<Float>(centreDown.x / centreDown.w, centreDown.y / centreDown.w, centreDown.z / centreDown.w)
        let d = -dot(cp, p3)
        return Float(-d / cp.z)
    }

    // MARK: Picking
    func pickObject(at location: CGPoint) -> Int32 {
        guard let pipeline = pickingRenderPipelineState,
              let colorTex = pickingTexture,
              let depthTex = pickingDepthTexture,
              !triangleBuffers.isEmpty else { return -1 }

        let scale: CGFloat = 1.0 // on iOS, points = pixels for non-Retina; use traitCollection for Retina
        let pixelX = Int(location.x * scale)
        let pixelY = Int(metalView.bounds.height * scale - location.y * scale)
        guard pixelX >= 0, pixelX < colorTex.width,
              pixelY >= 0, pixelY < colorTex.height else { return -1 }

        let cb = commandQueue.makeCommandBuffer()!

        let passDesc = MTLRenderPassDescriptor()
        passDesc.colorAttachments[0].texture = colorTex
        passDesc.colorAttachments[0].loadAction = .clear
        passDesc.colorAttachments[0].storeAction = .store
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0)
        passDesc.depthAttachment.texture = depthTex
        passDesc.depthAttachment.loadAction = .clear
        passDesc.depthAttachment.storeAction = .dontCare
        passDesc.depthAttachment.clearDepth = 1.0

        let encoder = cb.makeRenderCommandEncoder(descriptor: passDesc)!
        encoder.setRenderPipelineState(pipeline)
        encoder.setFrontFacing(.counterClockwise)
        encoder.setDepthStencilState(depthStencilState)
        encoder.setViewport(MTLViewport(originX: 0, originY: 0, width: Double(colorTex.width), height: Double(colorTex.height), znear: 0, zfar: 1))
        encoder.setVertexBytes(&constants, length: MemoryLayout<Constants>.size, index: 1)
        if let visBuffer = visibleStateBuffer, visibleStateCount > 0 {
            encoder.setFragmentBuffer(visBuffer, offset: 0, index: 2)
        } else {
            var one: Float = 1
            encoder.setFragmentBytes(&one, length: MemoryLayout<Float>.size, index: 2)
        }

        for tb in triangleBuffers {
            encoder.setVertexBuffer(tb.buffer, offset: 0, index: 0)
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: tb.indexCount, indexType: .uint32, indexBuffer: tb.indexBuffer, indexBufferOffset: 0)
        }
        encoder.endEncoding()

        let stagingBuffer = device.makeBuffer(length: 4, options: .storageModeShared)!
        let blitEncoder = cb.makeBlitCommandEncoder()!
        blitEncoder.copy(from: colorTex, sourceSlice: 0, sourceLevel: 0,
                         sourceOrigin: MTLOrigin(x: pixelX, y: pixelY, z: 0),
                         sourceSize: MTLSize(width: 1, height: 1, depth: 1),
                         to: stagingBuffer, destinationOffset: 0,
                         destinationBytesPerRow: 4, destinationBytesPerImage: 4)
        blitEncoder.endEncoding()
        cb.commit()
        cb.waitUntilCompleted()

        let pixelValue = stagingBuffer.contents().load(as: UInt32.self)
        return pixelValue == 0 ? -1 : Int32(bitPattern: pixelValue) - 1
    }

    // MARK: Buffer updates (called from ObjC++ bridge)
    @objc func updateVisibleStateBuffer() {
        let cnt = Int32(dataManager.visibleStateCount())
        guard cnt > 0 else { return }
        let ptr = dataManager.visibleStateData()
        guard ptr != nil else { return }
        visibleStateCount = Int(cnt)
        let byteCount = Int(cnt) * MemoryLayout<Float>.size
        if visibleStateBuffer?.length ?? 0 >= byteCount {
            visibleStateBuffer!.contents().copyMemory(from: UnsafeRawPointer(ptr!), byteCount: byteCount)
        } else {
            visibleStateBuffer = device.makeBuffer(bytes: UnsafeRawPointer(ptr!), length: byteCount, options: [])
        }
    }

    @objc func updateSelectionStateBuffer() {
        let cnt = Int32(dataManager.selectionStateCount())
        guard cnt > 0 else { return }
        let ptr = dataManager.selectionStateData()
        guard ptr != nil else { return }
        selectionStateCount = Int(cnt)
        let byteCount = Int(cnt) * MemoryLayout<Float>.size
        if selectionStateBuffer?.length ?? 0 >= byteCount {
            selectionStateBuffer!.contents().copyMemory(from: UnsafeRawPointer(ptr!), byteCount: byteCount)
        } else {
            selectionStateBuffer = device.makeBuffer(bytes: UnsafeRawPointer(ptr!), length: byteCount, options: [])
        }
    }

    // MARK: Buffer loading from DataManager
    func reloadTriangleBuffers() {
        triangleBuffers.removeAll()
        dataManager.initialiseTriangleBufferIterator()
        while !dataManager.triangleBufferIteratorEnded() {
            var bytes: Int = 0
            guard let vertexPtr = dataManager.currentTriangleBuffer(withSize: &bytes), bytes > 0 else {
                dataManager.advanceTriangleBufferIterator()
                continue
            }
            var indexBytes: Int = 0
            guard let indexPtr = dataManager.currentTriangleBufferIndices(withSize: &indexBytes), indexBytes > 0 else {
                dataManager.advanceTriangleBufferIterator()
                continue
            }

            let colour = dataManager.currentTriangleBufferColour()
            let colourSIMD = colour != nil ? SIMD4<Float>(colour![0], colour![1], colour![2], colour![3]) : SIMD4<Float>(1, 1, 1, 1)

            let buffer = device.makeBuffer(bytes: vertexPtr, length: bytes, options: [])!
            let indexBuffer = device.makeBuffer(bytes: indexPtr, length: indexBytes, options: [])!
            let indexCount = indexBytes / MemoryLayout<UInt32>.size

            triangleBuffers.append(BufferWithColour(buffer: buffer, indexBuffer: indexBuffer, indexCount: indexCount, type: "", colour: colourSIMD))
            dataManager.advanceTriangleBufferIterator()
        }
    }

    func reloadEdgeBuffers() {
        edgeBuffers.removeAll()
        dataManager.initialiseEdgeBufferIterator()
        while !dataManager.edgeBufferIteratorEnded() {
            var bytes: Int = 0
            guard let vertexPtr = dataManager.currentEdgeBuffer(withSize: &bytes) else {
                dataManager.advanceEdgeBufferIterator()
                continue
            }
            let colour = dataManager.currentEdgeBufferColour()
            let colourSIMD = colour != nil ? SIMD4<Float>(colour![0], colour![1], colour![2], colour![3]) : SIMD4<Float>(0, 0, 0, 1)
            let buffer = device.makeBuffer(bytes: vertexPtr, length: bytes, options: [])!
            edgeBuffers.append(BufferWithColour(buffer: buffer, indexBuffer: buffer, indexCount: 0, type: "", colour: colourSIMD))
            dataManager.advanceEdgeBufferIterator()
        }
    }

    func regenerateBoundingBoxBuffer() {
        guard let mins = dataManager.minCoordinates(),
              let mids = dataManager.midCoordinates(),
              let maxs = dataManager.maxCoordinates() else { return }
        let minCoords = [mins[0], mins[1], mins[2]]
        let midCoords = [mids[0], mids[1], mids[2]]
        let maxCoords = [maxs[0], maxs[1], maxs[2]]
        let range = dataManager.maxRange()

        let corners: [SIMD3<Float>] = [
            SIMD3<Float>((minCoords[0]-mids[0])/range, (minCoords[1]-mids[1])/range, (minCoords[2]-mids[2])/range),
            SIMD3<Float>((maxCoords[0]-mids[0])/range, (minCoords[1]-mids[1])/range, (minCoords[2]-mids[2])/range),
            SIMD3<Float>((maxCoords[0]-mids[0])/range, (maxCoords[1]-mids[1])/range, (minCoords[2]-mids[2])/range),
            SIMD3<Float>((minCoords[0]-mids[0])/range, (maxCoords[1]-mids[1])/range, (minCoords[2]-mids[2])/range),
            SIMD3<Float>((minCoords[0]-mids[0])/range, (minCoords[1]-mids[1])/range, (maxCoords[2]-mids[2])/range),
            SIMD3<Float>((maxCoords[0]-mids[0])/range, (minCoords[1]-mids[1])/range, (maxCoords[2]-mids[2])/range),
            SIMD3<Float>((maxCoords[0]-mids[0])/range, (maxCoords[1]-mids[1])/range, (maxCoords[2]-mids[2])/range),
            SIMD3<Float>((minCoords[0]-mids[0])/range, (maxCoords[1]-mids[1])/range, (maxCoords[2]-mids[2])/range),
        ]
        let edges: [Int] = [
            0,1, 1,2, 2,3, 3,0,
            4,5, 5,6, 6,7, 7,4,
            0,4, 1,5, 2,6, 3,7,
        ]
        var vertices = [Vertex]()
        for i in edges {
            vertices.append(Vertex(position: corners[i]))
        }
        boundingBoxBuffer = vertices.withUnsafeBytes {
            device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: [])
        }
    }

    // MARK: File loading
    func loadFile(url: URL) {
        let accessOK = url.startAccessingSecurityScopedResource()
        defer { if accessOK { url.stopAccessingSecurityScopedResource() } }
        let path = url.path
        print("Loading file: \(path)")
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            guard FileManager.default.fileExists(atPath: path) else {
                print("File not found: \(path)")
                return
            }
            self.dataManager.parse(path)
            print("Parse complete")
            self.dataManager.clearHelpers()
            self.dataManager.updateBoundsWithLastFile()
            print("Updating bounds")
            self.dataManager.triangulateLastFile()
            print("Triangulation complete")
            self.dataManager.generateEdgesForLastFile()
            print("Edges complete")
            self.dataManager.clearPolygonsOfLastFile()
            self.dataManager.regenerateTriangleBuffers(withMaximumSize: 16 * 1024 * 1024)
            print("Triangle buffers generated")
            self.dataManager.regenerateEdgeBuffers(withMaximumSize: 16 * 1024 * 1024)
            print("Edge buffers generated")

            DispatchQueue.main.async {
                print("Reloading GPU buffers")
                print("0 parsedFiles count: \(self.dataManager.numberOfParsedFiles())")
                self.reloadTriangleBuffers()
                print("1 Triangles: \(self.triangleBuffers.count)")
                self.reloadEdgeBuffers()
                print("2 Edges: \(self.edgeBuffers.count)")
                self.regenerateBoundingBoxBuffer()
                print("3 BBox: \(self.boundingBoxBuffer?.length ?? 0)")
                print("4 calling updateVisibleState")
                self.dataManager.updateVisibleStates()
                print("5 count: \(self.dataManager.visibleStateCount()) ptr: \(self.dataManager.visibleStateData() != nil ? "non-nil" : "nil")")
                self.updateVisibleStateBuffer()
                print("6 done visible")
                self.dataManager.updateSelectionStates()
                print("7 sel count: \(self.dataManager.selectionStateCount()) ptr: \(self.dataManager.selectionStateData() != nil ? "non-nil" : "nil")")
                self.updateSelectionStateBuffer()
                print("8 Load complete")
            }
        }
    }

    // MARK: Gesture recognizers
    func setupGestures() {
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.minimumNumberOfTouches = 1; pan.maximumNumberOfTouches = 1
        metalView.addGestureRecognizer(pan)

        let twoFingerPan = UIPanGestureRecognizer(target: self, action: #selector(handleOrbit(_:)))
        twoFingerPan.minimumNumberOfTouches = 2
        metalView.addGestureRecognizer(twoFingerPan)

        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        metalView.addGestureRecognizer(pinch)

        let rotation = UIRotationGestureRecognizer(target: self, action: #selector(handleRotation(_:)))
        metalView.addGestureRecognizer(rotation)

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        metalView.addGestureRecognizer(tap)
    }

    func updateConstants() {
        constants.modelMatrix = modelMatrix
        constants.modelViewProjectionMatrix = matrix_multiply(projectionMatrix, matrix_multiply(viewMatrix, modelMatrix))
        constants.modelMatrixInverseTransposed = matrix_upper_left_3x3(matrix: modelMatrix).inverse.transpose
        constants.viewMatrixInverse = viewMatrix.inverse
    }

    func orbit(angleX: Float, angleY: Float) {
        let forward = normalize(centre - eye)
        let right = normalize(cross(forward, SIMD3<Float>(0, 1, 0)))
        let up = SIMD3<Float>(0, 1, 0)
        let rx = matrix4x4_rotation(angle: angleX, axis: right)
        let ry = matrix4x4_rotation(angle: angleY, axis: up)
        let rotatedForward = (ry * rx) * simd_float4(forward.x, forward.y, forward.z, 0)
        let distance = simd_length(centre - eye)
        eye = centre - simd_float3(rotatedForward.x, rotatedForward.y, rotatedForward.z) * distance
        viewMatrix = matrix4x4_look_at(eye: eye, centre: centre, up: up)
        updateConstants()
    }

    @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
        let translation = gesture.translation(in: metalView)
        if gesture.state == .changed {
            let distance = simd_length(centre - eye)
            let sensitivity: Float = 0.003 * (fieldOfView / (.pi / 4)) * distance
            let motionInCamera = SIMD3<Float>(sensitivity * Float(-translation.x), sensitivity * Float(translation.y), 0)
            let cameraToObject = matrix_upper_left_3x3(matrix: matrix_multiply(viewMatrix, modelMatrix)).inverse
            let motionInObject = matrix_multiply(cameraToObject, motionInCamera)
            eye += motionInObject
            centre += motionInObject
            viewMatrix = matrix4x4_look_at(eye: eye, centre: centre, up: SIMD3<Float>(0, 1, 0))
            updateConstants()
        }
        gesture.setTranslation(.zero, in: metalView)
    }

    @objc func handleOrbit(_ gesture: UIPanGestureRecognizer) {
        let translation = gesture.translation(in: metalView)
        let delta = CGPoint(x: translation.x - lastPanTranslation.x, y: translation.y - lastPanTranslation.y)
        lastPanTranslation = translation
        if gesture.state == .began { lastPanTranslation = .zero }
        else if gesture.state == .changed {
            orbit(angleX: Float(-delta.y) * 0.005, angleY: Float(-delta.x) * 0.005)
        } else if gesture.state == .ended || gesture.state == .cancelled { lastPanTranslation = .zero }
    }

    @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        if gesture.state == .changed {
            let mag: Float = 1.0 + Float(gesture.scale - 1.0)
            fieldOfView = 2.0 * atanf(tanf(0.5 * fieldOfView) / mag)
            let w = Float(metalView.drawableSize.width)
            let h = Float(metalView.drawableSize.height)
            projectionMatrix = matrix4x4_perspective_shorter_dim(fieldOfView: fieldOfView, width: w, height: h, nearZ: 0.001, farZ: 100.0)
            updateConstants()
            gesture.scale = 1.0
        }
    }

    @objc func handleRotation(_ gesture: UIRotationGestureRecognizer) {
        if gesture.state == .changed {
            let axisInCamera = SIMD3<Float>(0.0, 0.0, 1.0)
            let cameraToObject = matrix_upper_left_3x3(matrix: matrix_multiply(viewMatrix, modelMatrix)).inverse
            let axisInObject = matrix_multiply(cameraToObject, axisInCamera)
            modelRotationMatrix = matrix_multiply(modelRotationMatrix, matrix4x4_rotation(angle: Float(gesture.rotation), axis: axisInObject))
            modelMatrix = matrix_multiply(matrix_multiply(modelShiftBackMatrix, modelRotationMatrix), modelTranslationToCentreOfRotationMatrix)
            updateConstants()
            gesture.rotation = 0
        }
    }

    @objc func handleTap(_ gesture: UITapGestureRecognizer) {
        let location = gesture.location(in: metalView)
        let objectId = pickObject(at: location)
        if objectId >= 0 {
            let _ = dataManager.setBestHitFromObjectId(objectId)
        }
    }

    // MARK: Floating buttons
    func setupFloatingButtons() {
        let buttonConfig = UIImage.SymbolConfiguration(pointSize: 18, weight: .medium)
        openButton = makeFloatingButton(systemName: "folder.fill", config: buttonConfig, action: #selector(openFile))
        objectsButton = makeFloatingButton(systemName: "list.bullet", config: buttonConfig, action: #selector(showObjects))
        homeButton = makeFloatingButton(systemName: "house.fill", config: buttonConfig, action: #selector(goHome))

        let bottomStack = UIStackView(arrangedSubviews: [openButton, objectsButton, homeButton])
        bottomStack.axis = .horizontal
        bottomStack.spacing = 12
        bottomStack.distribution = .equalSpacing
        bottomStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(bottomStack)

        NSLayoutConstraint.activate([
            bottomStack.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -12),
            bottomStack.centerXAnchor.constraint(equalTo: view.centerXAnchor),
        ])
    }

    func makeFloatingButton(systemName: String, config: UIImage.SymbolConfiguration, action: Selector) -> UIButton {
        let isPad = UIDevice.current.userInterfaceIdiom == .pad
        let size: CGFloat = isPad ? 40 : 32
        let image = UIImage(systemName: systemName, withConfiguration: config)
        let button = UIButton(type: .system)
        button.setImage(image, for: .normal)
        button.tintColor = .white
        button.backgroundColor = UIColor(white: 0, alpha: 0.4)
        button.layer.cornerRadius = size / 2
        button.layer.cornerCurve = .continuous
        button.translatesAutoresizingMaskIntoConstraints = false
        button.widthAnchor.constraint(equalToConstant: size).isActive = true
        button.heightAnchor.constraint(equalToConstant: size).isActive = true
        button.addTarget(self, action: action, for: .touchUpInside)
        return button
    }

    // MARK: Actions
    @objc func openFile() {
        let types: [UTType] = [.json, .xml, UTType(filenameExtension: "obj")!, UTType(filenameExtension: "off")!, UTType(filenameExtension: "poly")!, UTType(filenameExtension: "gml")!, UTType(filenameExtension: "azulview")!].compactMap { $0 }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = true
        present(picker, animated: true)
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

    @objc func goHome() {
        fieldOfView = 1.047197551196598
        modelTranslationToCentreOfRotationMatrix = matrix_identity_float4x4
        modelRotationMatrix = matrix_identity_float4x4
        modelShiftBackMatrix = matrix4x4_translation(shift: centre)
        modelMatrix = matrix_multiply(matrix_multiply(modelShiftBackMatrix, modelRotationMatrix), modelTranslationToCentreOfRotationMatrix)
        viewMatrix = matrix4x4_look_at(eye: eye, centre: centre, up: SIMD3<Float>(0.0, 1.0, 0.0))
        projectionMatrix = matrix4x4_perspective_shorter_dim(fieldOfView: fieldOfView, width: Float(metalView.drawableSize.width), height: Float(metalView.drawableSize.height), nearZ: 0.001, farZ: 100.0)
        updateConstants()
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
        attrsVC.title = ident.isEmpty ? (dataManager.type(ofItem: item) ?? "") : ident
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
