import { useState } from 'react'

function CameraSettings({ isOpen, onClose, onSettingsChange }) {
  const [settings, setSettings] = useState({
    resolution: '1920x1080',
    framerate: '30',
    quality: 'high',
    exposure: 0,
    brightness: 0,
    contrast: 0,
    saturation: 0,
    autoFocus: true,
    stabilization: true,
    nightMode: false,
    recordingFormat: 'mp4',
    compressionLevel: 'medium'
  })

  const handleSettingChange = (key, value) => {
    const newSettings = { ...settings, [key]: value }
    setSettings(newSettings)
    onSettingsChange?.(newSettings)
  }

  if (!isOpen) return null

  return (
    <div className="fixed inset-0 bg-black/50 z-50 flex items-center justify-center">
      <div className="bg-white rounded-lg p-6 max-w-md w-full mx-4 max-h-[90vh] overflow-y-auto">
        <div className="flex justify-between items-center mb-4">
          <h3 className="text-lg font-bold text-gray-800">Camera Settings</h3>
          <button
            onClick={onClose}
            className="text-gray-500 hover:text-gray-700 text-xl"
          >
            ×
          </button>
        </div>

        <div className="space-y-4">
          {/* Resolution */}
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">
              Resolution
            </label>
            <select
              value={settings.resolution}
              onChange={(e) => handleSettingChange('resolution', e.target.value)}
              className="w-full p-2 border border-gray-300 rounded-md"
            >
              <option value="1920x1080">1920x1080 (Full HD)</option>
              <option value="1280x720">1280x720 (HD)</option>
              <option value="854x480">854x480 (480p)</option>
              <option value="640x360">640x360 (360p)</option>
            </select>
          </div>

          {/* Frame Rate */}
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">
              Frame Rate
            </label>
            <select
              value={settings.framerate}
              onChange={(e) => handleSettingChange('framerate', e.target.value)}
              className="w-full p-2 border border-gray-300 rounded-md"
            >
              <option value="60">60 FPS</option>
              <option value="30">30 FPS</option>
              <option value="24">24 FPS</option>
              <option value="15">15 FPS</option>
            </select>
          </div>

          {/* Quality */}
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">
              Video Quality
            </label>
            <select
              value={settings.quality}
              onChange={(e) => handleSettingChange('quality', e.target.value)}
              className="w-full p-2 border border-gray-300 rounded-md"
            >
              <option value="ultra">Ultra (High Bitrate)</option>
              <option value="high">High</option>
              <option value="medium">Medium</option>
              <option value="low">Low (Bandwidth Saving)</option>
            </select>
          </div>

          {/* Image Adjustments */}
          <div className="space-y-3">
            <h4 className="font-medium text-gray-700">Image Adjustments</h4>
            
            {/* Exposure */}
            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Exposure: {settings.exposure > 0 ? '+' : ''}{settings.exposure}
              </label>
              <input
                type="range"
                min="-100"
                max="100"
                value={settings.exposure}
                onChange={(e) => handleSettingChange('exposure', parseInt(e.target.value))}
                className="w-full"
              />
            </div>

            {/* Brightness */}
            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Brightness: {settings.brightness > 0 ? '+' : ''}{settings.brightness}
              </label>
              <input
                type="range"
                min="-100"
                max="100"
                value={settings.brightness}
                onChange={(e) => handleSettingChange('brightness', parseInt(e.target.value))}
                className="w-full"
              />
            </div>

            {/* Contrast */}
            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Contrast: {settings.contrast > 0 ? '+' : ''}{settings.contrast}
              </label>
              <input
                type="range"
                min="-100"
                max="100"
                value={settings.contrast}
                onChange={(e) => handleSettingChange('contrast', parseInt(e.target.value))}
                className="w-full"
              />
            </div>

            {/* Saturation */}
            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Saturation: {settings.saturation > 0 ? '+' : ''}{settings.saturation}
              </label>
              <input
                type="range"
                min="-100"
                max="100"
                value={settings.saturation}
                onChange={(e) => handleSettingChange('saturation', parseInt(e.target.value))}
                className="w-full"
              />
            </div>
          </div>

          {/* Toggle Settings */}
          <div className="space-y-3">
            <h4 className="font-medium text-gray-700">Camera Features</h4>
            
            <label className="flex items-center">
              <input
                type="checkbox"
                checked={settings.autoFocus}
                onChange={(e) => handleSettingChange('autoFocus', e.target.checked)}
                className="mr-2"
              />
              <span className="text-sm text-gray-600">Auto Focus</span>
            </label>

            <label className="flex items-center">
              <input
                type="checkbox"
                checked={settings.stabilization}
                onChange={(e) => handleSettingChange('stabilization', e.target.checked)}
                className="mr-2"
              />
              <span className="text-sm text-gray-600">Image Stabilization</span>
            </label>

            <label className="flex items-center">
              <input
                type="checkbox"
                checked={settings.nightMode}
                onChange={(e) => handleSettingChange('nightMode', e.target.checked)}
                className="mr-2"
              />
              <span className="text-sm text-gray-600">Night Mode</span>
            </label>
          </div>

          {/* Recording Settings */}
          <div className="space-y-3">
            <h4 className="font-medium text-gray-700">Recording</h4>
            
            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Recording Format
              </label>
              <select
                value={settings.recordingFormat}
                onChange={(e) => handleSettingChange('recordingFormat', e.target.value)}
                className="w-full p-2 border border-gray-300 rounded-md"
              >
                <option value="mp4">MP4 (Recommended)</option>
                <option value="avi">AVI</option>
                <option value="mov">MOV</option>
                <option value="webm">WebM</option>
              </select>
            </div>

            <div>
              <label className="block text-sm text-gray-600 mb-1">
                Compression Level
              </label>
              <select
                value={settings.compressionLevel}
                onChange={(e) => handleSettingChange('compressionLevel', e.target.value)}
                className="w-full p-2 border border-gray-300 rounded-md"
              >
                <option value="none">None (Lossless)</option>
                <option value="low">Low Compression</option>
                <option value="medium">Medium Compression</option>
                <option value="high">High Compression</option>
              </select>
            </div>
          </div>
        </div>

        {/* Action Buttons */}
        <div className="flex gap-2 mt-6">
          <button
            onClick={() => {
              // Reset to defaults
              const defaultSettings = {
                resolution: '1920x1080',
                framerate: '30',
                quality: 'high',
                exposure: 0,
                brightness: 0,
                contrast: 0,
                saturation: 0,
                autoFocus: true,
                stabilization: true,
                nightMode: false,
                recordingFormat: 'mp4',
                compressionLevel: 'medium'
              }
              setSettings(defaultSettings)
              onSettingsChange?.(defaultSettings)
            }}
            className="px-4 py-2 text-gray-600 border border-gray-300 rounded hover:bg-gray-50"
          >
            Reset Defaults
          </button>
          <button
            onClick={onClose}
            className="flex-1 px-4 py-2 bg-blue-500 text-white rounded hover:bg-blue-600"
          >
            Apply Settings
          </button>
        </div>
      </div>
    </div>
  )
}

export default CameraSettings