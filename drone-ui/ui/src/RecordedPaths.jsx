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
                
                {/* Right: Path Preview */}
                <div className="w-64 h-40 bg-white rounded border border-gray-200 flex-shrink-0">
                  <svg
                    viewBox="0 0 1000 600"
                    className="w-full h-full"
                    style={{ background: '#fafafa' }}
                  >
                    {/* Draw lines connecting waypoints */}
                    {path.points.length > 1 && (
                      <polyline
                        points={path.points.map(p => `${p.x},${p.y}`).join(' ')}
                        fill="none"
                        stroke="#6b7280"
                        strokeWidth="3"
                      />
                    )}
                    
                    {/* Draw waypoint circles */}
                    {path.points.map((point, index) => (
                      <circle
                        key={index}
                        cx={point.x}
                        cy={point.y}
                        r="6"
                        fill="#374151"
                        stroke="white"
                        strokeWidth="2"
                      />
                    ))}
                  </svg>
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

