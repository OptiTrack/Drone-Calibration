import { useState } from 'react'
import PathPlanner3D from './PathPlanner3D'
import RecordedPaths from './RecordedPaths'
import RecordedVideos from './RecordedVideos'
import CameraFeed from './CameraFeed'

function App() {
    const [drawerOpen, setDrawerOpen] = useState(false)
    const [activeView, setActiveView] = useState('home')
    const [paths, setPaths] = useState([])
    const [recordings, setRecordings] = useState([])
    const [draftPoints, setDraftPoints] = useState([])

    const handleSavePath = (name, points) => {
        const newPath = {
            id: crypto.randomUUID(),
            name: name || 'Untitled Path',
            points: [...points],
            createdAt: Date.now()
        }
        setPaths(prev => [...prev, newPath])
    }

    const handleDeletePath = (id) => {
        setPaths(prev => prev.filter(p => p.id !== id))
    }

    const handleLoadToPlanner = (points) => {
        setDraftPoints([...points])
        setActiveView('planner')
    }

    const handleSaveRecording = (recording) => {
        setRecordings(prev => [...prev, recording])
    }

    const handleDeleteRecording = (id) => {
        setRecordings(prev => {
            const recording = prev.find(r => r.id === id)
            if (recording && recording.blobUrl) {
                URL.revokeObjectURL(recording.blobUrl)
            }
            return prev.filter(r => r.id !== id)
        })
    }

    const handlePlayRecording = (recording) => {
        // Open the recording in a new window/tab for playback
        window.open(recording.blobUrl, '_blank')
    }

    const switchView = (view) => {
        setActiveView(view)
        setDrawerOpen(false)
    }

    return (
        <div className="flex h-screen flex-col">
            {/* Drawer Backdrop */}
            {drawerOpen && (
                <div
                    className="fixed inset-0 bg-black/40 z-40"
                    onClick={() => setDrawerOpen(false)}
                />
            )}

            {/* Drawer Panel */}
            <div className={`fixed left-0 top-0 h-full w-[280px] bg-white border-r border-gray-200 shadow-xl z-50 transition-transform duration-300 ${drawerOpen ? 'translate-x-0' : '-translate-x-full'}`}>
                <div className="p-6 flex flex-col gap-4">
                    <div className="text-gray-800 text-2xl font-bold mb-4">CaliDrone</div>

                    <button
                        onClick={() => switchView('home')}
                        className={`w-full ${activeView === 'home' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Home</span>
                    </button>

                    <button
                        onClick={() => switchView('planner')}
                        className={`w-full ${activeView === 'planner' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Path Planner</span>
                    </button>

                    <button
                        onClick={() => switchView('recorder')}
                        className={`w-full ${activeView === 'recorder' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Recorded Paths</span>
                    </button>

                    <button
                        onClick={() => switchView('videos')}
                        className={`w-full ${activeView === 'videos' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Recorded Videos</span>
                    </button>

                    <button
                        onClick={() => switchView('camera')}
                        className={`w-full ${activeView === 'camera' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Camera Feed</span>
                    </button>

                    <button
                        onClick={() => switchView('settings')}
                        className={`w-full ${activeView === 'settings' ? 'bg-gray-300' : 'bg-gray-200'} hover:bg-gray-300 text-gray-800 py-4 px-4 rounded-lg transition text-left`}
                    >
                        <span className="font-medium">Settings</span>
                    </button>
                </div>
            </div>

            {/* Top Navbar */}
            <div className="bg-white border-b border-gray-300 px-6 py-3 flex items-center gap-6">
                {/* Hamburger Menu */}
                <button
                    onClick={() => setDrawerOpen(!drawerOpen)}
                    className="flex flex-col gap-1 p-2 hover:bg-gray-100 rounded transition"
                >
                    <div className="w-5 h-0.5 bg-gray-800"></div>
                    <div className="w-5 h-0.5 bg-gray-800"></div>
                    <div className="w-5 h-0.5 bg-gray-800"></div>
                </button>

                {/* Status Items */}
                <span className="flex items-center text-gray-800">
                    <span className="w-2 h-2 bg-green-500 rounded-full mr-2"></span>
                    Connected
                </span>
                <span className="text-gray-800">Drone: Alpha</span>
                <span className="text-gray-800">Battery: 78%</span>
                <span className="text-gray-800">Mode: Stabilize</span>
                <span className="text-gray-800">Motive: Connected</span>
            </div>

            {/* Main Content */}
            <div className="flex-1 bg-gray-100 overflow-auto">
                {activeView === 'home' && (
                    <div className="p-6 h-full">
                        <div className="grid grid-cols-2 gap-4 h-full">
                            {/* Path Planner Box - Clickable */}
                            <button
                                onClick={() => setActiveView('planner')}
                                className="bg-gray-200 hover:bg-gray-300 rounded-xl p-4 flex items-center justify-center transition cursor-pointer"
                            >
                                <span className="text-gray-800 text-xl font-medium">Path Planner</span>
                            </button>

                            {/* Camera Feed Box */}
                            <div className="bg-gray-900 rounded-xl p-2 h-full">
                                <CameraFeed 
                                    compact={true} 
                                    onSaveRecording={handleSaveRecording}
                                />
                            </div>

                            {/* Telemetry Box */}
                            <div className="bg-gray-200 rounded-xl p-4 flex items-center justify-center">
                                <span className="text-gray-800 text-xl font-medium">Telemetry / Data Output</span>
                            </div>

                            {/* Controls Box */}
                            <div className="bg-gray-200 rounded-xl p-4 flex items-center justify-center">
                                <span className="text-gray-800 text-xl font-medium">Controls</span>
                            </div>
                        </div>
                    </div>
                )}

                {activeView === 'planner' && (
                    <PathPlanner3D
                        points={draftPoints}
                        onPointsChange={setDraftPoints}
                        onSavePath={handleSavePath}
                        onGoToRecorder={() => switchView('recorder')}
                    />
                )}

                {activeView === 'recorder' && (
                    <RecordedPaths
                        paths={paths}
                        onDeletePath={handleDeletePath}
                        onLoadToPlanner={handleLoadToPlanner}
                    />
                )}

                {activeView === 'camera' && (
                    <div className="p-6 h-full">
                        <div className="h-full bg-gray-900 rounded-xl p-4">
                            <CameraFeed 
                                compact={false} 
                                onSaveRecording={handleSaveRecording}
                            />
                        </div>
                    </div>
                )}

                {activeView === 'videos' && (
                    <RecordedVideos
                        recordings={recordings}
                        onDeleteRecording={handleDeleteRecording}
                        onPlayRecording={handlePlayRecording}
                    />
                )}

                {activeView === 'settings' && (
                    <div className="p-6 flex items-center justify-center h-full">
                        <div className="bg-gray-200 rounded-xl p-8">
                            <span className="text-gray-800 text-xl font-medium">Settings (Coming Soon)</span>
                        </div>
                    </div>
                )}
            </div>
        </div>
    )
}

export default App

