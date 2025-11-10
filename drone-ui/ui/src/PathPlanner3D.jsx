import { useState, useRef, useMemo, useEffect } from 'react'
import { Canvas, useFrame, useThree } from '@react-three/fiber'
import { OrbitControls, Grid, PerspectiveCamera, Line, Html } from '@react-three/drei'
import * as THREE from 'three'

// 3D Waypoint component
function Waypoint({ position, index, onSelect, isSelected }) {
  const meshRef = useRef()
  
  useFrame((state) => {
    if (meshRef.current) {
      meshRef.current.rotation.y += 0.01
    }
  })

  return (
    <group position={position}>
      <mesh
        ref={meshRef}
        onClick={(e) => {
          e.stopPropagation()
          onSelect(index)
        }}
        onPointerOver={(e) => {
          e.stopPropagation()
          document.body.style.cursor = 'pointer'
        }}
        onPointerOut={() => {
          document.body.style.cursor = 'default'
        }}
      >
        <sphereGeometry args={[0.3, 16, 16]} />
        <meshStandardMaterial 
          color={isSelected ? '#3b82f6' : '#374151'} 
          emissive={isSelected ? '#3b82f6' : '#000000'}
          emissiveIntensity={isSelected ? 0.3 : 0}
        />
      </mesh>
      <Html position={[0, 0.6, 0]} center style={{ pointerEvents: 'none' }}>
        <div className="bg-gray-800 text-white px-2 py-1 rounded text-xs font-bold">
          {index + 1}
        </div>
      </Html>
      <Html position={[0, -0.6, 0]} center style={{ pointerEvents: 'none' }}>
        <div className="text-xs text-gray-600 bg-white/80 px-1 py-0.5 rounded whitespace-nowrap">
          {/* position is in Three.js coords [userY, userZ, userX], display as [userX, userY, userZ] */}
          ({position[2].toFixed(1)}, {position[0].toFixed(1)}, {position[1].toFixed(1)})
        </div>
      </Html>
    </group>
  )
}

// Path line component
// Map user coordinates (X,Y,Z) to Three.js coordinates (Y, Z, X) where:
// User X → Three.js Z (red axis, toward viewer)
// User Y → Three.js X (green axis, to the right)
// User Z → Three.js Y (blue axis, vertical up)
function PathLine({ points }) {
  const linePoints = useMemo(() => {
    return points.map(p => new THREE.Vector3(p.y || 0, p.z || 0, p.x || 0))
  }, [points])

  if (linePoints.length < 2) return null

  return (
    <Line
      points={linePoints}
      color="#6b7280"
      lineWidth={3}
    />
  )
}

// Ground plane click handler
function GroundPlane({ onAddPoint, gridSize }) {
  const planeRef = useRef()

  const handleClick = (event) => {
    event.stopPropagation()
    const { point } = event
    // Snap to grid
    // In Three.js: point is (x, y, z) where y is up
    // We map: user (X, Y, Z) → Three.js (Y, Z, X)
    // So: user X = Three.js Z, user Y = Three.js X, user Z = Three.js Y
    const snapSize = 0.5
    const userX = Math.round(point.z / snapSize) * snapSize  // Three.js Z → user X
    const userY = Math.round(point.x / snapSize) * snapSize  // Three.js X → user Y
    const userZ = Math.round(point.y / snapSize) * snapSize  // Three.js Y → user Z (vertical)
    // Default height of 1 unit above ground (user Z = 1)
    onAddPoint(userX, userY, userZ || 1)
  }

  return (
    <mesh
      ref={planeRef}
      rotation={[-Math.PI / 2, 0, 0]}
      position={[0, 0, 0]}
      onClick={handleClick}
      onPointerOver={() => {
        document.body.style.cursor = 'crosshair'
      }}
      onPointerOut={() => {
        document.body.style.cursor = 'default'
      }}
    >
      <planeGeometry args={[gridSize * 2, gridSize * 2]} />
      <meshStandardMaterial color="#f3f4f6" transparent opacity={0.1} />
    </mesh>
  )
}

// Custom OrbitControls with mouse button configuration
function CustomOrbitControls() {
  const { camera, gl } = useThree()
  const controlsRef = useRef()

  useEffect(() => {
    if (controlsRef.current) {
      const controls = controlsRef.current
      
      // In Three.js OrbitControls, mouse button values are:
      // 0 = ROTATE
      // 1 = DOLLY (zoom)
      // 2 = PAN
      
      // Configure: left disabled (null), middle for pan (2), right for rotate (0)
      try {
        // Try to set mouseButtons object
        if (controls.mouseButtons) {
          controls.mouseButtons.LEFT = null
          controls.mouseButtons.MIDDLE = 2  // PAN
          controls.mouseButtons.RIGHT = 0   // ROTATE
        } else {
          // Create the object if it doesn't exist
          controls.mouseButtons = {
            LEFT: null,
            MIDDLE: 2,  // PAN
            RIGHT: 0    // ROTATE
          }
        }
      } catch (e) {
        console.warn('Could not set mouseButtons:', e)
      }
      
      // Ensure features are enabled
      controls.enablePan = true
      controls.enableRotate = true
      controls.enableZoom = true
    }
  }, [])

  return (
    <OrbitControls
      ref={controlsRef}
      args={[camera, gl.domElement]}
      enableDamping
      dampingFactor={0.05}
      enablePan={true}
      enableRotate={true}
      enableZoom={true}
      mouseButtons={{
        LEFT: null,
        MIDDLE: 2,  // PAN
        RIGHT: 0    // ROTATE
      }}
    />
  )
}

// Right-Hand Rule Coordinate Frame
// Index finger (X) → Green, points along +X (to the right, horizontal)
// Middle finger (Y) → Red, points along +Y (toward viewer/out of screen, horizontal)
// Thumb (Z) → Blue, points along +Z (upward, vertical)
// Note: In Three.js, Y is up, so we map: User (X,Y,Z) → Three.js (X, Z, Y)
function CoordinateFrame({ length = 5 }) {
  const axisRadius = 0.02
  const arrowRadius = 0.1
  const arrowLength = 0.3
  
  return (
    <group>
      {/* X-axis (Green) - Index finger - extends along +X (to the right, horizontal) */}
      <group>
        {/* X-axis cylinder - extends from origin to +X */}
        <mesh position={[length / 2, 0, 0]} rotation={[0, 0, -Math.PI / 2]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#00ff00" />
        </mesh>
        {/* X-axis arrow - points in +X direction (rotate cone from +Y to +X) */}
        <mesh position={[length, 0, 0]} rotation={[0, 0, -Math.PI / 2]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#00ff00" />
        </mesh>
      </group>
      
      {/* Y-axis (Red) - Middle finger - extends along +Y (toward viewer/out of screen, horizontal) */}
      <group>
        {/* Y-axis cylinder - extends from origin to +Y (rotate from +Y to +Z in Three.js space) */}
        <mesh position={[0, 0, length / 2]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#ff0000" />
        </mesh>
        {/* Y-axis arrow - points in +Y direction (toward viewer, which is +Z in Three.js) */}
        <mesh position={[0, 0, length]} rotation={[Math.PI / 2, 0, 0]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#ff0000" />
        </mesh>
      </group>
      
      {/* Z-axis (Blue) - Thumb - extends along +Z (upward, vertical) */}
      <group>
        {/* Z-axis cylinder - extends from origin to +Z (vertical, which is +Y in Three.js) */}
        <mesh position={[0, length / 2, 0]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#0000ff" />
        </mesh>
        {/* Z-axis arrow - points in +Z direction (vertical up, which is +Y in Three.js) */}
        <mesh position={[0, length, 0]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#0000ff" />
        </mesh>
      </group>
    </group>
  )
}

// Main 3D Scene
function Scene3D({ points, onAddPoint, selectedIndex, onSelectPoint, gridSize = 20 }) {
  return (
    <>
      {/* Lighting */}
      <ambientLight intensity={0.5} />
      <directionalLight position={[10, 10, 5]} intensity={1} />
      <pointLight position={[-10, 10, -10]} intensity={0.5} />

      {/* Grid */}
      <Grid
        args={[gridSize, gridSize]}
        cellSize={1}
        cellThickness={0.5}
        cellColor="#e5e7eb"
        sectionSize={5}
        sectionThickness={1}
        sectionColor="#d1d5db"
        fadeDistance={gridSize}
        fadeStrength={1}
      />

      {/* Ground plane for clicking */}
      <GroundPlane onAddPoint={onAddPoint} gridSize={gridSize} />

      {/* Path line */}
      <PathLine points={points} />

      {/* Waypoints */}
      {/* Map user coordinates (X,Y,Z) to Three.js coordinates (Y, Z, X) where:
          User X → Three.js Z (red axis, toward viewer)
          User Y → Three.js X (green axis, to the right)  
          User Z → Three.js Y (blue axis, vertical up) */}
      {points.map((point, index) => (
        <Waypoint
          key={index}
          position={[point.y || 0, point.z || 0, point.x || 0]}
          index={index}
          onSelect={onSelectPoint}
          isSelected={selectedIndex === index}
        />
      ))}

      {/* Right-Hand Rule Coordinate Frame */}
      <CoordinateFrame length={5} />
    </>
  )
}

function PathPlanner3D({ points, onPointsChange, onSavePath, onGoToRecorder, compact = false }) {
  const [pathName, setPathName] = useState('')
  const [selectedIndex, setSelectedIndex] = useState(null)
  const [manualInput, setManualInput] = useState({ x: '', y: '', z: '' })
  const [gridSize, setGridSize] = useState(20)
  const canvasRef = useRef()

  // Ensure points have z coordinate (backward compatibility)
  const normalizedPoints = useMemo(() => {
    return points.map(p => ({
      x: p.x || 0,
      y: p.y || 0,
      z: p.z !== undefined ? p.z : 0
    }))
  }, [points])

  const handleAddPoint = (x, y, z) => {
    onPointsChange([...normalizedPoints, { x, y, z }])
  }

  const handleAddManualPoint = () => {
    const x = parseFloat(manualInput.x) || 0
    const y = parseFloat(manualInput.y) || 0
    const z = parseFloat(manualInput.z) || 0
    handleAddPoint(x, y, z)
    setManualInput({ x: '', y: '', z: '' })
  }

  const handleUndo = () => {
    if (normalizedPoints.length > 0) {
      onPointsChange(normalizedPoints.slice(0, -1))
      setSelectedIndex(null)
    }
  }

  const handleClear = () => {
    onPointsChange([])
    setPathName('')
    setSelectedIndex(null)
  }

  const handleSave = () => {
    if (normalizedPoints.length === 0) {
      alert('Please add at least one waypoint before saving.')
      return
    }
    onSavePath(pathName, normalizedPoints)
    handleClear()
  }

  const handleDeleteSelected = () => {
    if (selectedIndex !== null) {
      const newPoints = normalizedPoints.filter((_, i) => i !== selectedIndex)
      onPointsChange(newPoints)
      setSelectedIndex(null)
    }
  }

  const handleUpdateSelected = () => {
    if (selectedIndex !== null && manualInput.x && manualInput.y && manualInput.z) {
      const newPoints = [...normalizedPoints]
      newPoints[selectedIndex] = {
        x: parseFloat(manualInput.x) || 0,
        y: parseFloat(manualInput.y) || 0,
        z: parseFloat(manualInput.z) || 0
      }
      onPointsChange(newPoints)
      setManualInput({ x: '', y: '', z: '' })
      setSelectedIndex(null)
    }
  }

  const handleSelectPoint = (index) => {
    if (selectedIndex === index) {
      setSelectedIndex(null)
      setManualInput({ x: '', y: '', z: '' })
    } else {
      setSelectedIndex(index)
      const point = normalizedPoints[index]
      setManualInput({
        x: point.x.toString(),
        y: point.y.toString(),
        z: point.z.toString()
      })
    }
  }

  if (compact) {
    return (
      <div className="h-full flex flex-col">
        <div className="p-4 border-b border-gray-200">
          <h3 className="text-lg font-bold text-gray-800 mb-2">3D Path Planner</h3>
          <div className="flex gap-2 mb-2">
            <input
              type="text"
              placeholder="Path name..."
              value={pathName}
              onChange={(e) => setPathName(e.target.value)}
              className="px-3 py-1.5 text-sm border border-gray-300 rounded text-gray-800 focus:outline-none focus:border-gray-400 flex-1"
            />
          </div>
          <div className="flex gap-2 flex-wrap">
            <button
              onClick={handleUndo}
              disabled={normalizedPoints.length === 0}
              className="px-3 py-1.5 text-sm bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded transition"
            >
              Undo
            </button>
            <button
              onClick={handleClear}
              disabled={normalizedPoints.length === 0}
              className="px-3 py-1.5 text-sm bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded transition"
            >
              Clear
            </button>
            <button
              onClick={handleSave}
              className="px-3 py-1.5 text-sm bg-gray-800 hover:bg-gray-700 text-white rounded transition"
            >
              Save
            </button>
          </div>
          <div className="text-xs text-gray-600 mt-2">
            Click on the ground to add waypoints {normalizedPoints.length > 0 && `(${normalizedPoints.length})`}
          </div>
        </div>
        <div className="flex-1 overflow-hidden">
          <Canvas ref={canvasRef} camera={{ position: [10, 10, 10], fov: 50 }}>
            <PerspectiveCamera makeDefault position={[10, 10, 10]} fov={50} />
            <CustomOrbitControls />
            <Scene3D
              points={normalizedPoints}
              onAddPoint={handleAddPoint}
              selectedIndex={selectedIndex}
              onSelectPoint={handleSelectPoint}
              gridSize={gridSize}
            />
          </Canvas>
        </div>
      </div>
    )
  }

  return (
    <div className="p-6 h-full flex gap-4">
      {/* Left Sidebar - Controls */}
      <div className="w-80 flex-shrink-0 bg-white rounded-xl p-6 border border-gray-200 flex flex-col gap-4 overflow-y-auto">
        <h2 className="text-2xl font-bold text-gray-800">3D Path Planner</h2>
        
        {/* Path Name */}
        <div>
          <label className="text-sm font-semibold text-gray-700 block mb-2">Path Name</label>
          <input
            type="text"
            placeholder="Enter path name..."
            value={pathName}
            onChange={(e) => setPathName(e.target.value)}
            className="w-full px-3 py-2 border border-gray-300 rounded-lg text-gray-800 focus:outline-none focus:border-gray-400"
          />
        </div>

        {/* Action Buttons */}
        <div className="flex flex-wrap gap-2">
          <button
            onClick={handleUndo}
            disabled={normalizedPoints.length === 0}
            className="flex-1 px-3 py-2 bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded-lg transition font-medium text-sm"
          >
            Undo
          </button>
          <button
            onClick={handleClear}
            disabled={normalizedPoints.length === 0}
            className="flex-1 px-3 py-2 bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded-lg transition font-medium text-sm"
          >
            Clear
          </button>
          <button
            onClick={handleSave}
            className="flex-1 px-3 py-2 bg-gray-800 hover:bg-gray-700 text-white rounded-lg transition font-medium text-sm"
          >
            Save Path
          </button>
          <button
            onClick={onGoToRecorder}
            className="w-full px-3 py-2 bg-gray-200 hover:bg-gray-300 text-gray-800 rounded-lg transition font-medium text-sm"
          >
            View Saved Paths
          </button>
        </div>

        {/* Manual Input Section */}
        <div className="bg-gray-50 rounded-lg p-3">
          <label className="text-sm font-semibold text-gray-700 block mb-2">
            {selectedIndex !== null ? `Edit Waypoint #${selectedIndex + 1}` : 'Add Waypoint'}
          </label>
          <div className="flex gap-2 mb-2">
            <input
              type="number"
              step="0.1"
              placeholder="X"
              value={manualInput.x}
              onChange={(e) => setManualInput({ ...manualInput, x: e.target.value })}
              className="w-16 px-2 py-1.5 text-sm border border-gray-300 rounded text-gray-800 focus:outline-none focus:border-gray-400"
            />
            <input
              type="number"
              step="0.1"
              placeholder="Y"
              value={manualInput.y}
              onChange={(e) => setManualInput({ ...manualInput, y: e.target.value })}
              className="w-16 px-2 py-1.5 text-sm border border-gray-300 rounded text-gray-800 focus:outline-none focus:border-gray-400"
            />
            <input
              type="number"
              step="0.1"
              placeholder="Z"
              value={manualInput.z}
              onChange={(e) => setManualInput({ ...manualInput, z: e.target.value })}
              className="w-16 px-2 py-1.5 text-sm border border-gray-300 rounded text-gray-800 focus:outline-none focus:border-gray-400"
            />
          </div>
          {selectedIndex !== null ? (
            <div className="flex gap-2">
              <button
                onClick={handleUpdateSelected}
                className="flex-1 px-3 py-1.5 text-sm bg-blue-600 hover:bg-blue-700 text-white rounded transition font-medium"
              >
                Update
              </button>
              <button
                onClick={handleDeleteSelected}
                className="flex-1 px-3 py-1.5 text-sm bg-red-600 hover:bg-red-700 text-white rounded transition font-medium"
              >
                Delete
              </button>
              <button
                onClick={() => {
                  setSelectedIndex(null)
                  setManualInput({ x: '', y: '', z: '' })
                }}
                className="flex-1 px-3 py-1.5 text-sm bg-gray-200 hover:bg-gray-300 text-gray-800 rounded transition font-medium"
              >
                Cancel
              </button>
            </div>
          ) : (
            <button
              onClick={handleAddManualPoint}
              className="w-full px-3 py-1.5 text-sm bg-gray-800 hover:bg-gray-700 text-white rounded transition font-medium"
            >
              Add Point
            </button>
          )}
        </div>

        {/* Grid Size Control */}
        <div>
          <label className="text-sm font-semibold text-gray-700 block mb-2">
            Grid Size: {gridSize}m
          </label>
          <input
            type="range"
            min="5"
            max="50"
            step="5"
            value={gridSize}
            onChange={(e) => setGridSize(parseInt(e.target.value))}
            className="w-full"
          />
        </div>

        {/* Info */}
        <div className="text-xs text-gray-600 space-y-1">
          <p>• Left click on ground to add waypoints</p>
          <p>• Left click waypoint to edit</p>
          <p>• Middle mouse: pan</p>
          <p>• Right mouse: rotate</p>
          <p>• Scroll: zoom</p>
          {normalizedPoints.length > 0 && (
            <p className="mt-2 font-medium text-sm">
              {normalizedPoints.length} waypoint{normalizedPoints.length !== 1 ? 's' : ''} added
            </p>
          )}
        </div>
      </div>

      {/* Right Side - 3D Canvas */}
      <div className="flex-1 bg-white rounded-xl border border-gray-200 overflow-hidden">
        <Canvas ref={canvasRef} camera={{ position: [10, 10, 10], fov: 50 }}>
          <PerspectiveCamera makeDefault position={[10, 10, 10]} fov={50} />
          <CustomOrbitControls />
          <Scene3D
            points={normalizedPoints}
            onAddPoint={handleAddPoint}
            selectedIndex={selectedIndex}
            onSelectPoint={handleSelectPoint}
            gridSize={gridSize}
          />
        </Canvas>
      </div>
    </div>
  )
}

export default PathPlanner3D



