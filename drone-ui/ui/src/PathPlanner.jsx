import { useState, useRef } from 'react'

function PathPlanner({ points, onPointsChange, onSavePath, onGoToRecorder, compact = false }) {
  const [pathName, setPathName] = useState('')
  const svgRef = useRef(null)

  const handleSvgClick = (e) => {
    if (!svgRef.current) return
    
    const svg = svgRef.current
    const pt = svg.createSVGPoint()
    
    // Set the point to the mouse position
    pt.x = e.clientX
    pt.y = e.clientY
    
    // Convert screen coordinates to SVG coordinates
    const svgP = pt.matrixTransform(svg.getScreenCTM().inverse())
    
    onPointsChange([...points, { x: svgP.x, y: svgP.y }])
  }

  const handleUndo = () => {
    if (points.length > 0) {
      onPointsChange(points.slice(0, -1))
    }
  }

  const handleClear = () => {
    onPointsChange([])
    setPathName('')
  }

  const handleSave = () => {
    if (points.length === 0) {
      alert('Please add at least one waypoint before saving.')
      return
    }
    onSavePath(pathName, points)
    handleClear()
  }

  if (compact) {
    return (
      <div className="h-full flex flex-col">
        {/* Compact Header */}
        <div className="p-4 border-b border-gray-200">
          <h3 className="text-lg font-bold text-gray-800 mb-2">Path Planner</h3>
          <div className="flex gap-2 mb-2">
            <input
              type="text"
              placeholder="Path name..."
              value={pathName}
              onChange={(e) => setPathName(e.target.value)}
              className="px-3 py-1.5 text-sm border border-gray-300 rounded text-gray-800 focus:outline-none focus:border-gray-400 flex-1"
            />
          </div>
          <div className="flex gap-2">
            <button
              onClick={handleUndo}
              disabled={points.length === 0}
              className="px-3 py-1.5 text-sm bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded transition"
            >
              Undo
            </button>
            <button
              onClick={handleClear}
              disabled={points.length === 0}
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
            Click to add waypoints {points.length > 0 && `(${points.length})`}
          </div>
        </div>

        {/* SVG Canvas */}
        <div className="flex-1 overflow-hidden">
          <svg
            ref={svgRef}
            viewBox="0 0 1000 600"
            className="w-full h-full cursor-crosshair"
            onClick={handleSvgClick}
            style={{ 
              background: 'repeating-linear-gradient(0deg, transparent, transparent 19px, #f3f4f6 19px, #f3f4f6 20px), repeating-linear-gradient(90deg, transparent, transparent 19px, #f3f4f6 19px, #f3f4f6 20px)',
              backgroundColor: '#fafafa'
            }}
          >
            {/* Draw lines connecting waypoints */}
            {points.length > 1 && (
              <polyline
                points={points.map(p => `${p.x},${p.y}`).join(' ')}
                fill="none"
                stroke="#6b7280"
                strokeWidth="2"
              />
            )}
            
            {/* Draw waypoint circles */}
            {points.map((point, index) => (
              <g key={index}>
                <circle
                  cx={point.x}
                  cy={point.y}
                  r="8"
                  fill="#374151"
                  stroke="white"
                  strokeWidth="2"
                />
                <text
                  x={point.x}
                  y={point.y - 15}
                  textAnchor="middle"
                  fill="#374151"
                  fontSize="14"
                  fontWeight="bold"
                >
                  {index + 1}
                </text>
              </g>
            ))}
          </svg>
        </div>
      </div>
    )
  }

  return (
    <div className="p-6 h-full flex flex-col gap-4">
      <div className="bg-white rounded-xl p-6 border border-gray-200">
        <h2 className="text-2xl font-bold text-gray-800 mb-4">Path Planner</h2>
        
        {/* Controls */}
        <div className="flex flex-wrap gap-3 mb-4">
          <input
            type="text"
            placeholder="Path name..."
            value={pathName}
            onChange={(e) => setPathName(e.target.value)}
            className="px-4 py-2 border border-gray-300 rounded-lg text-gray-800 focus:outline-none focus:border-gray-400 flex-1 min-w-[200px]"
          />
          <button
            onClick={handleUndo}
            disabled={points.length === 0}
            className="px-4 py-2 bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded-lg transition font-medium"
          >
            Undo
          </button>
          <button
            onClick={handleClear}
            disabled={points.length === 0}
            className="px-4 py-2 bg-gray-200 hover:bg-gray-300 disabled:bg-gray-100 disabled:text-gray-400 text-gray-800 rounded-lg transition font-medium"
          >
            Clear
          </button>
          <button
            onClick={handleSave}
            className="px-4 py-2 bg-gray-800 hover:bg-gray-700 text-white rounded-lg transition font-medium"
          >
            Save Path
          </button>
          <button
            onClick={onGoToRecorder}
            className="px-4 py-2 bg-gray-200 hover:bg-gray-300 text-gray-800 rounded-lg transition font-medium"
          >
            View Saved Paths
          </button>
        </div>

        {/* Info */}
        <div className="text-sm text-gray-600 mb-4">
          Click on the canvas below to add waypoints. {points.length > 0 && `(${points.length} point${points.length !== 1 ? 's' : ''})`}
        </div>
      </div>

      {/* SVG Canvas */}
      <div className="flex-1 bg-white rounded-xl border border-gray-200 overflow-hidden">
        <svg
          ref={svgRef}
          viewBox="0 0 1000 600"
          className="w-full h-full cursor-crosshair"
          onClick={handleSvgClick}
          style={{ 
            background: 'repeating-linear-gradient(0deg, transparent, transparent 19px, #f3f4f6 19px, #f3f4f6 20px), repeating-linear-gradient(90deg, transparent, transparent 19px, #f3f4f6 19px, #f3f4f6 20px)',
            backgroundColor: '#fafafa'
          }}
        >
          {/* Grid pattern is in the style above */}
          
          {/* Draw lines connecting waypoints */}
          {points.length > 1 && (
            <polyline
              points={points.map(p => `${p.x},${p.y}`).join(' ')}
              fill="none"
              stroke="#6b7280"
              strokeWidth="2"
            />
          )}
          
          {/* Draw waypoint circles */}
          {points.map((point, index) => (
            <g key={index}>
              <circle
                cx={point.x}
                cy={point.y}
                r="8"
                fill="#374151"
                stroke="white"
                strokeWidth="2"
              />
              <text
                x={point.x}
                y={point.y - 15}
                textAnchor="middle"
                fill="#374151"
                fontSize="14"
                fontWeight="bold"
              >
                {index + 1}
              </text>
            </g>
          ))}
        </svg>
      </div>
    </div>
  )
}

export default PathPlanner

