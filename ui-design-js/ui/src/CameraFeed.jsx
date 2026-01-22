import { useState, useRef, useEffect, useCallback } from 'react'
import { BsCamera, BsGearFill, BsFullscreen, BsFullscreenExit, BsRecordCircle, BsStopCircle } from 'react-icons/bs'

function CameraFeed({ compact = false, showControls = true, onSaveRecording, recordingSettings = {} }) {
  const [isRecording, setIsRecording] = useState(false)
  const [feedSource, setFeedSource] = useState('live') // 'live' or 'demo'
  const [error, setError] = useState(null)
  const [isFullscreen, setIsFullscreen] = useState(false)
  const [zoom, setZoom] = useState(1)
  const [showSettings, setShowSettings] = useState(false)
  const [cameraSettings, _setCameraSettings] = useState({
    quality: 'high',
    format: 'mp4',
    framerate: 30,
    ...recordingSettings
  })
  const [recordingStartTime, setRecordingStartTime] = useState(null)
  const [recordingDuration, setRecordingDuration] = useState(0)
  const videoRef = useRef(null)
  const containerRef = useRef(null)
  const mediaRecorderRef = useRef(null)
  const recordedChunksRef = useRef([])

  // Demo feed URL - using the created demo image
  const demoImageUrl = '/demo-drone-feed.jpg'
  
  // Simulated live feed URL (in real implementation, this would be the drone's camera stream)
  const _liveFeedUrl = 'ws://localhost:8080/camera-stream' // WebSocket or HTTP stream

  const initializeFeed = useCallback(async () => {
    if (feedSource === 'live') {
      try {
        // In a real implementation, you would connect to the drone's camera stream
        // For now, we'll attempt to use the user's webcam as a demo
        const stream = await navigator.mediaDevices.getUserMedia({ 
          video: { 
            width: { ideal: 1920 },
            height: { ideal: 1080 }
          } 
        })
        if (videoRef.current) {
          videoRef.current.srcObject = stream
        }
        setError(null)
      } catch (err) {
        console.warn('Could not access camera:', err)
        setError('Camera not available - showing demo image')
        setFeedSource('demo')
      }
    }
  }, [feedSource, setError, setFeedSource])

  useEffect(() => {
    // Initialize camera feed
    initializeFeed()
    
    // Store current refs for cleanup
    const currentVideoRef = videoRef.current
    const currentMediaRecorderRef = mediaRecorderRef.current
    
    // Cleanup on unmount
    return () => {
      if (currentVideoRef && currentVideoRef.srcObject) {
        const tracks = currentVideoRef.srcObject.getTracks()
        tracks.forEach(track => track.stop())
      }
      if (currentMediaRecorderRef && currentMediaRecorderRef.state !== 'inactive') {
        currentMediaRecorderRef.stop()
      }
    }
  }, [initializeFeed])

  // Recording duration timer
  useEffect(() => {
    let interval = null
    if (isRecording && recordingStartTime) {
      interval = setInterval(() => {
        setRecordingDuration(Math.floor((Date.now() - recordingStartTime) / 1000))
      }, 1000)
    } else {
      setRecordingDuration(0)
    }
    return () => {
      if (interval) clearInterval(interval)
    }
  }, [isRecording, recordingStartTime])

  const handleRecord = async () => {
    if (!isRecording) {
      // Start recording
      try {
        if (feedSource === 'live' && videoRef.current && videoRef.current.srcObject) {
          recordedChunksRef.current = []
          const options = {
            mimeType: `video/${cameraSettings.format}`,
            videoBitsPerSecond: getVideoBitrate(cameraSettings.quality)
          }
          
          mediaRecorderRef.current = new MediaRecorder(videoRef.current.srcObject, options)
          
          mediaRecorderRef.current.ondataavailable = (event) => {
            if (event.data.size > 0) {
              recordedChunksRef.current.push(event.data)
            }
          }
          
          mediaRecorderRef.current.onstop = () => {
            const blob = new Blob(recordedChunksRef.current, { 
              type: `video/${cameraSettings.format}` 
            })
            saveRecording(blob)
          }
          
          mediaRecorderRef.current.start()
          const startTime = Date.now()
          setRecordingStartTime(startTime)
          setIsRecording(true)
        } else {
          // Demo mode - simulate recording
          const startTime = Date.now()
          setRecordingStartTime(startTime)
          setIsRecording(true)
          // Auto-stop demo recording after 10 seconds
          setTimeout(() => {
            if (isRecording) {
              handleRecord()
            }
          }, 10000)
        }
      } catch (err) {
        console.error('Error starting recording:', err)
        setError('Recording failed to start')
      }
    } else {
      // Stop recording
      if (mediaRecorderRef.current && mediaRecorderRef.current.state === 'recording') {
        mediaRecorderRef.current.stop()
      } else {
        // Demo mode - create a mock recording
        const mockBlob = new Blob(['mock video data'], { type: `video/${cameraSettings.format}` })
        saveRecording(mockBlob)
      }
      setIsRecording(false)
      setRecordingStartTime(null)
    }
  }

  const getVideoBitrate = (quality) => {
    const bitrates = {
      ultra: 8000000,  // 8 Mbps
      high: 4000000,   // 4 Mbps
      medium: 2000000, // 2 Mbps
      low: 1000000     // 1 Mbps
    }
    return bitrates[quality] || bitrates.high
  }

  const saveRecording = (blob) => {
    if (onSaveRecording) {
      const recording = {
        id: crypto.randomUUID(),
        name: `Recording ${new Date().toLocaleString()}`,
        blob: blob,
        blobUrl: URL.createObjectURL(blob),
        size: blob.size,
        duration: recordingDuration,
        format: cameraSettings.format,
        quality: cameraSettings.quality,
        createdAt: Date.now(),
        thumbnailUrl: null // Could generate thumbnail from video
      }
      
      onSaveRecording(recording)
    }
  }

  const handleScreenshot = () => {
    // Take a screenshot of the current feed
    if (feedSource === 'live' && videoRef.current) {
      const canvas = document.createElement('canvas')
      canvas.width = videoRef.current.videoWidth
      canvas.height = videoRef.current.videoHeight
      const ctx = canvas.getContext('2d')
      ctx.drawImage(videoRef.current, 0, 0)
      
      // Download the screenshot
      canvas.toBlob(blob => {
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = `drone-screenshot-${Date.now()}.png`
        document.body.appendChild(a)
        a.click()
        document.body.removeChild(a)
        URL.revokeObjectURL(url)
      })
    }
    console.log('Screenshot captured')
  }

  const toggleFullscreen = () => {
    if (!isFullscreen) {
      containerRef.current?.requestFullscreen?.()
    } else {
      document.exitFullscreen?.()
    }
    setIsFullscreen(!isFullscreen)
  }

  const handleZoomChange = (newZoom) => {
    setZoom(Math.max(0.5, Math.min(3, newZoom)))
  }

  const FeedControls = () => (
    <div className="absolute top-2 right-2 flex gap-2 bg-black/50 rounded-lg p-2">
      {/* Feed Source Toggle */}
      <button
        onClick={() => setFeedSource(feedSource === 'live' ? 'demo' : 'live')}
        className="bg-blue-500 hover:bg-blue-600 text-white px-3 py-1 rounded text-sm"
        title="Toggle feed source"
      >
        {feedSource === 'live' ? 'Live' : 'Demo'}
      </button>

      {/* Record Button */}
      <button
        onClick={handleRecord}
        className={`${isRecording ? 'bg-red-500 hover:bg-red-600' : 'bg-gray-500 hover:bg-gray-600'} text-white px-3 py-1 rounded text-sm flex items-center gap-1`}
        title="Record video"
      >
        {isRecording ? <BsStopCircle size={14} /> : <BsRecordCircle size={14} />}
        {isRecording ? 'Stop' : 'Record'}
      </button>

      {/* Screenshot Button */}
      <button
        onClick={handleScreenshot}
        className="bg-green-500 hover:bg-green-600 text-white px-3 py-1 rounded text-sm flex items-center gap-1"
        title="Take screenshot"
      >
        <BsCamera size={14} />
      </button>

      {/* Fullscreen Button */}
      {!compact && (
        <button
          onClick={toggleFullscreen}
          className="bg-gray-500 hover:bg-gray-600 text-white px-3 py-1 rounded text-sm flex items-center gap-1"
          title="Toggle fullscreen"
        >
          {isFullscreen ? <BsFullscreenExit size={14} /> : <BsFullscreen size={14} />}
          {isFullscreen ? 'Exit' : 'Full'}
        </button>
      )}

      {/* Settings Button */}
      <button
        onClick={() => setShowSettings(true)}
        className="bg-purple-500 hover:bg-purple-600 text-white px-3 py-1 rounded text-sm flex items-center gap-1"
        title="Camera settings"
      >
        <BsGearFill size={14} />
      </button>
    </div>
  )

  const ZoomControls = () => (
    <div className="absolute bottom-2 right-2 flex items-center gap-2 bg-black/50 rounded-lg p-2">
      <button
        onClick={() => handleZoomChange(zoom - 0.25)}
        className="bg-gray-500 hover:bg-gray-600 text-white px-2 py-1 rounded text-sm"
        disabled={zoom <= 0.5}
      >
        -
      </button>
      <span className="text-white text-sm min-w-[3rem] text-center">
        {Math.round(zoom * 100)}%
      </span>
      <button
        onClick={() => handleZoomChange(zoom + 0.25)}
        className="bg-gray-500 hover:bg-gray-600 text-white px-2 py-1 rounded text-sm"
        disabled={zoom >= 3}
      >
        +
      </button>
    </div>
  )

  const containerClasses = compact 
    ? "relative bg-black rounded-lg overflow-hidden h-full"
    : "relative bg-black rounded-xl overflow-hidden w-full h-full"

  return (
    <div 
      ref={containerRef}
      className={containerClasses}
    >
      {feedSource === 'live' ? (
        <video
          ref={videoRef}
          autoPlay
          muted
          playsInline
          className="w-full h-full object-cover"
          style={{ transform: `scale(${zoom})` }}
          onLoadedMetadata={() => setError(null)}
          onError={() => {
            setError('Video stream error')
            setFeedSource('demo')
          }}
        />
      ) : (
        <div className="w-full h-full flex items-center justify-center relative">
          <img
            src={demoImageUrl}
            alt="Drone Camera Feed"
            className="w-full h-full object-cover"
            style={{ transform: `scale(${zoom})` }}
            onError={() => setError('Demo image not found')}
          />
          <div className="absolute inset-0 flex items-center justify-center">
            <div className="bg-black/30 text-white px-4 py-2 rounded-lg">
              Demo Camera Feed
            </div>
          </div>
        </div>
      )}

      {error && (
        <div className="absolute inset-0 flex items-center justify-center bg-gray-800">
          <div className="text-center text-white">
            <div className="text-red-400 text-4xl mb-2">⚠</div>
            <div className="text-sm">{error}</div>
            <button
              onClick={() => setFeedSource('demo')}
              className="mt-2 bg-blue-500 hover:bg-blue-600 text-white px-3 py-1 rounded text-sm"
            >
              Use Demo Feed
            </button>
          </div>
        </div>
      )}

      {showControls && <FeedControls />}
      {showControls && !compact && <ZoomControls />}

      {/* Settings placeholder - settings modal would go here */}
      {showSettings && (
        <div className="absolute inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
          <div className="bg-white rounded-lg p-6 max-w-md">
            <h3 className="text-lg font-bold mb-4">Camera Settings</h3>
            <p className="text-gray-600 mb-4">Settings panel coming soon...</p>
            <button 
              onClick={() => setShowSettings(false)}
              className="bg-gray-800 text-white px-4 py-2 rounded"
            >
              Close
            </button>
          </div>
        </div>
      )}

      {/* Status Overlay */}
      <div className="absolute bottom-2 left-2 bg-black/50 text-white text-xs px-2 py-1 rounded">
        <div>Drone Camera</div>
        <div className="text-green-400">
          {feedSource === 'live' ? '● LIVE' : '● DEMO'}
        </div>
        {isRecording && (
          <div className="text-red-400 flex items-center gap-1">
            <div className="w-2 h-2 bg-red-500 rounded-full animate-pulse"></div>
            REC {Math.floor(recordingDuration / 60)}:{(recordingDuration % 60).toString().padStart(2, '0')}
          </div>
        )}
      </div>
    </div>
  )
}

export default CameraFeed