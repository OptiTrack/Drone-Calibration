import { Canvas } from '@react-three/fiber'
import { OrbitControls, Grid, Line } from '@react-three/drei'
import * as THREE from 'three'

// Coordinate Frame for preview (same as main planner)
function CoordinateFrame({ length = 3 }) {
  const axisRadius = 0.02
  const arrowRadius = 0.08
  const arrowLength = 0.2
  
  return (
    <group>
      {/* X-axis (Green) - to the right */}
      <group>
        <mesh position={[length / 2, 0, 0]} rotation={[0, 0, -Math.PI / 2]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#00ff00" />
        </mesh>
        <mesh position={[length, 0, 0]} rotation={[0, 0, -Math.PI / 2]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#00ff00" />
        </mesh>
      </group>
      
      {/* Y-axis (Red) - toward viewer */}
      <group>
        <mesh position={[0, 0, length / 2]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#ff0000" />
        </mesh>
        <mesh position={[0, 0, length]} rotation={[Math.PI / 2, 0, 0]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#ff0000" />
        </mesh>
      </group>
      
      {/* Z-axis (Blue) - vertical up */}
      <group>
        <mesh position={[0, length / 2, 0]}>
          <cylinderGeometry args={[axisRadius, axisRadius, length, 8]} />
          <meshStandardMaterial color="#0000ff" />
        </mesh>
        <mesh position={[0, length, 0]}>
          <coneGeometry args={[arrowRadius, arrowLength, 8]} />
          <meshStandardMaterial color="#0000ff" />
        </mesh>
      </group>
    </group>
  )
}

// 3D Preview Component for a single path
// Uses same coordinate mapping as PathPlanner3D: User (X,Y,Z) → Three.js (Y, Z, X)
function PathPreview3D({ points }) {
  const normalizedPoints = points.map(p => ({
    x: p.x || 0,
    y: p.y || 0,
    z: p.z !== undefined ? p.z : 0
  }))

  // Map user coordinates (X,Y,Z) to Three.js coordinates (Y, Z, X)
  const linePoints = normalizedPoints.map(p => new THREE.Vector3(p.y || 0, p.z || 0, p.x || 0))

  // Calculate bounding box in Three.js space for camera positioning
  const bounds = linePoints.length > 0
    ? linePoints.reduce((acc, p) => {
        return {
          minX: Math.min(acc.minX, p.x),
          maxX: Math.max(acc.maxX, p.x),
          minY: Math.min(acc.minY, p.y),
          maxY: Math.max(acc.maxY, p.y),
          minZ: Math.min(acc.minZ, p.z),
          maxZ: Math.max(acc.maxZ, p.z)
        }
      }, {
        minX: linePoints[0].x,
        maxX: linePoints[0].x,
        minY: linePoints[0].y,
        maxY: linePoints[0].y,
        minZ: linePoints[0].z,
        maxZ: linePoints[0].z
      })
    : { minX: 0, maxX: 0, minY: 0, maxY: 0, minZ: 0, maxZ: 0 }

  const centerX = (bounds.minX + bounds.maxX) / 2
  const centerY = (bounds.minY + bounds.maxY) / 2
  const centerZ = (bounds.minZ + bounds.maxZ) / 2
  const size = Math.max(
    bounds.maxX - bounds.minX,
    bounds.maxY - bounds.minY,
    bounds.maxZ - bounds.minZ
  ) || 5

  const cameraDistance = Math.max(size * 1.5, 5)

  return (
    <Canvas camera={{ position: [cameraDistance, cameraDistance, cameraDistance], fov: 50 }}>
      <ambientLight intensity={0.5} />
      <directionalLight position={[5, 5, 5]} intensity={0.8} />
      
      <Grid
        args={[Math.max(size, 5), Math.max(size, 5)]}
        cellSize={1}
        cellThickness={0.3}
        cellColor="#e5e7eb"
        sectionSize={5}
        sectionThickness={0.5}
        sectionColor="#d1d5db"
        fadeDistance={size}
        fadeStrength={1}
      />

      {/* Coordinate Frame */}
      <CoordinateFrame length={Math.min(size / 2, 3)} />

      {linePoints.length > 1 && (
        <Line
          points={linePoints}
          color="#6b7280"
          lineWidth={2}
        />
      )}

      {/* Waypoints - mapped to Three.js coordinates */}
      {normalizedPoints.map((point, index) => (
        <mesh key={index} position={[point.y || 0, point.z || 0, point.x || 0]}>
          <sphereGeometry args={[0.15, 8, 8]} />
          <meshStandardMaterial color="#374151" />
        </mesh>
      ))}

      <OrbitControls
        enableZoom={false}
        enablePan={false}
        autoRotate
        autoRotateSpeed={1}
        minPolarAngle={Math.PI / 4}
        maxPolarAngle={Math.PI / 2.2}
      />
    </Canvas>
  )
}

function RecordedPaths({ paths, onDeletePath, onLoadToPlanner }) {
  const formatDate = (timestamp) => {
    const date = new Date(timestamp)
    return date.toLocaleString()
  }

  if (paths.length === 0) {
    return (
      <div className="p-6 h-full flex items-center justify-center">
        <div className="bg-white rounded-xl p-12 border border-gray-200 text-center">
          <h2 className="text-2xl font-bold text-gray-800 mb-2">No Recorded Paths</h2>
          <p className="text-gray-600">Create a path in the Path Planner to see it here.</p>
        </div>
      </div>
    )
  }

  return (
    <div className="p-6 h-full">
      <div className="bg-white rounded-xl p-6 border border-gray-200 h-full flex flex-col">
        <h2 className="text-2xl font-bold text-gray-800 mb-6">Recorded Paths</h2>
        
        <div className="flex-1 overflow-auto">
          <div className="grid gap-4">
            {paths.map((path) => (
              <div key={path.id} className="bg-gray-50 rounded-lg p-4 border border-gray-200 flex gap-4">
                {/* Left: Path Info */}
                <div className="flex-1">
                  <h3 className="text-lg font-semibold text-gray-800 mb-2">{path.name}</h3>
                  <div className="text-sm text-gray-600 space-y-1">
                    <div>Waypoints: {path.points.length}</div>
                    <div>Created: {formatDate(path.createdAt)}</div>
                  </div>
                  
                  {/* Actions */}
                  <div className="flex gap-2 mt-4">
                    <button
                      onClick={() => onLoadToPlanner(path.points)}
                      className="px-4 py-2 bg-gray-800 hover:bg-gray-700 text-white rounded-lg transition text-sm font-medium"
                    >
                      Load into Planner
                    </button>
                    <button
                      onClick={() => {
                        if (confirm(`Delete path "${path.name}"?`)) {
                          onDeletePath(path.id)
                        }
                      }}
                      className="px-4 py-2 bg-gray-200 hover:bg-gray-300 text-gray-800 rounded-lg transition text-sm font-medium"
                    >
                      Delete
                    </button>
                  </div>
                </div>
                
                {/* Right: 3D Path Preview */}
                <div className="w-64 h-40 bg-white rounded border border-gray-200 flex-shrink-0 overflow-hidden">
                  <PathPreview3D points={path.points} />
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  )
}

export default RecordedPaths

