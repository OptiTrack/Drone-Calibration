import { useState } from 'react'
import { BsPlay, BsDownload, BsTrash, BsCameraVideo } from 'react-icons/bs'

function RecordedVideos({ recordings, onDeleteRecording, onPlayRecording }) {
  const [_selectedRecording, _setSelectedRecording] = useState(null)

  const formatDate = (timestamp) => {
    const date = new Date(timestamp)
    return date.toLocaleString()
  }

  const formatDuration = (duration) => {
    const minutes = Math.floor(duration / 60)
    const seconds = duration % 60
    return `${minutes}:${seconds.toString().padStart(2, '0')}`
  }

  const formatFileSize = (bytes) => {
    if (bytes === 0) return '0 B'
    const k = 1024
    const sizes = ['B', 'KB', 'MB', 'GB']
    const i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
  }

  const handleDownload = (recording) => {
    // Create a download link for the recorded video
    const link = document.createElement('a')
    link.href = recording.blobUrl
    link.download = `${recording.name}.${recording.format}`
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
  }

  const handleExportAll = () => {
    // Export all recordings as a zip or download them individually
    recordings.forEach((recording, index) => {
      setTimeout(() => {
        handleDownload(recording)
      }, index * 1000) // Stagger downloads by 1 second
    })
  }

  if (recordings.length === 0) {
    return (
      <div className="p-6 h-full flex items-center justify-center">
        <div className="bg-white rounded-xl p-12 border border-gray-200 text-center">
          <h2 className="text-2xl font-bold text-gray-800 mb-2">No Recorded Videos</h2>
          <p className="text-gray-600">Record videos using the camera feed to see them here.</p>
        </div>
      </div>
    )
  }

  return (
    <div className="p-6 h-full">
      <div className="bg-white rounded-xl p-6 border border-gray-200 h-full flex flex-col">
        <div className="flex justify-between items-center mb-6">
          <h2 className="text-2xl font-bold text-gray-800">Recorded Videos</h2>
          <button
            onClick={handleExportAll}
            className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg transition text-sm font-medium"
          >
            Download All
          </button>
        </div>
        
        <div className="flex-1 overflow-auto">
          <div className="grid gap-4">
            {recordings.map((recording) => (
              <div key={recording.id} className="bg-gray-50 rounded-lg p-4 border border-gray-200 flex gap-4">
                {/* Left: Recording Info */}
                <div className="flex-1">
                  <h3 className="text-lg font-semibold text-gray-800 mb-2">{recording.name}</h3>
                  <div className="text-sm text-gray-600 space-y-1">
                    <div>Duration: {formatDuration(recording.duration)}</div>
                    <div>Format: {recording.format.toUpperCase()}</div>
                    <div>Quality: {recording.quality}</div>
                    <div>Size: {formatFileSize(recording.size)}</div>
                    <div>Recorded: {formatDate(recording.createdAt)}</div>
                  </div>
                  
                  {/* Actions */}
                  <div className="flex gap-2 mt-4">
                    <button
                      onClick={() => onPlayRecording(recording)}
                      className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded-lg transition text-sm font-medium flex items-center gap-2"
                    >
                      <BsPlay size={14} />
                      Play
                    </button>
                    <button
                      onClick={() => handleDownload(recording)}
                      className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg transition text-sm font-medium flex items-center gap-2"
                    >
                      <BsDownload size={14} />
                      Download
                    </button>
                    <button
                      onClick={() => {
                        if (confirm(`Delete recording "${recording.name}"?`)) {
                          onDeleteRecording(recording.id)
                        }
                      }}
                      className="px-4 py-2 bg-gray-200 hover:bg-gray-300 text-gray-800 rounded-lg transition text-sm font-medium flex items-center gap-2"
                    >
                      <BsTrash size={14} />
                      Delete
                    </button>
                  </div>
                </div>
                
                {/* Right: Video Preview */}
                <div className="w-64 h-40 bg-black rounded border border-gray-200 flex-shrink-0 relative overflow-hidden">
                  {recording.thumbnailUrl ? (
                    <img
                      src={recording.thumbnailUrl}
                      alt={`${recording.name} thumbnail`}
                      className="w-full h-full object-cover"
                    />
                  ) : (
                    <div className="w-full h-full flex items-center justify-center">
                      <div className="text-white text-center">
                        <BsCameraVideo size={32} className="mx-auto mb-2" />
                        <div className="text-sm">Video Preview</div>
                      </div>
                    </div>
                  )}
                  
                  {/* Play overlay */}
                  <div className="absolute inset-0 flex items-center justify-center bg-black bg-opacity-30 opacity-0 hover:opacity-100 transition-opacity">
                    <button
                      onClick={() => onPlayRecording(recording)}
                      className="bg-white bg-opacity-80 rounded-full p-3 hover:bg-opacity-100 transition"
                    >
                      <BsPlay className="w-6 h-6 text-gray-800" />
                    </button>
                  </div>
                  
                  {/* Duration badge */}
                  <div className="absolute bottom-2 right-2 bg-black bg-opacity-70 text-white px-2 py-1 rounded text-xs">
                    {formatDuration(recording.duration)}
                  </div>
                </div>
              </div>
            ))}
          </div>
        </div>

        {/* Recording Statistics */}
        <div className="mt-6 pt-4 border-t border-gray-200">
          <div className="grid grid-cols-3 gap-4 text-center">
            <div>
              <div className="text-2xl font-bold text-gray-800">{recordings.length}</div>
              <div className="text-sm text-gray-600">Total Videos</div>
            </div>
            <div>
              <div className="text-2xl font-bold text-gray-800">
                {formatDuration(recordings.reduce((total, rec) => total + rec.duration, 0))}
              </div>
              <div className="text-sm text-gray-600">Total Duration</div>
            </div>
            <div>
              <div className="text-2xl font-bold text-gray-800">
                {formatFileSize(recordings.reduce((total, rec) => total + rec.size, 0))}
              </div>
              <div className="text-sm text-gray-600">Total Size</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

export default RecordedVideos