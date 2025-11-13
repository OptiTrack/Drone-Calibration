#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QTimer>

#include "data_processor.h"

DataProcessor::DataProcessor(
    const std::vector<FrameData>& frames,
    QObject* parent
)
    : QObject(parent),
      m_frames(frames)
{
    skeletonMetrics = std::make_unique<SkeletonMetrics>(this);
}


void DataProcessor::onFramesUpdated(const FrameData& signalFrame) 
{
    qDebug() << "DataProcessor: New frames signal received";

    // Ensure there are enough frames to compute velocity and acceleration
    if (!(m_previousFrame && m_secondPreviousFrame)) {
        
        qDebug() << "Not enough frames to compute metrics.";

        m_secondPreviousFrame = m_previousFrame;
        m_previousFrame = signalFrame; 

        return;
    }

    // Compute rigid body metrics
    MetricsData rbMetrics = rigidBodyMetrics.computeMetricsForFrame(signalFrame, *m_previousFrame, *m_secondPreviousFrame);

    // Compute skeleton metrics
    MetricsData skelMetrics =  skeletonMetrics->computeMetricsForFrame(signalFrame);

    emit metricsComputed(rbMetrics, skelMetrics);

    m_secondPreviousFrame = m_previousFrame;
    m_previousFrame = signalFrame; 
}

void DataProcessor::receiveMaps(const std::unordered_map<int, std::string>& rigidBodies,
                                const std::unordered_map<int, std::string>& skeletons,
                                const std::unordered_map<int, std::unordered_map<int, std::string>>& bones)
{
            qDebug() << rigidBodies << skeletons << bones;

            rigidBodyMetrics.setRigidBodyMap(rigidBodies);
            rigidBodyMetrics.createInverseMaps();

            skeletonMetrics->setSkeletonMap(skeletons);
            skeletonMetrics->setBoneMap(bones);
            skeletonMetrics->createInverseMaps();

            qDebug() << "DataProcessor: maps set";



            emit sendAssets(skeletonMetrics->getSkeletonNameToId(), 
                    rigidBodyMetrics.getRigidBodyNameToId());
}

void DataProcessor::receiveAssets(AssetSettings assetSettings)
{
    QString skeletonAsset = assetSettings.skeleton;
    skeletonMetrics->setAsset(skeletonAsset);

    QString rigidBodyAsset = assetSettings.rigidBody;
    rigidBodyMetrics.setAsset(rigidBodyAsset);
}

void DataProcessor::receiveNamingConvention(ConnectionSettings connectionSettings)
{
    skeletonMetrics->setNamingConvention(connectionSettings.namingConvention);
}

void DataProcessor::receiveMetricSettings(QJsonArray rigidMetricsSettings, QJsonArray bodyMetricsSettings)
{
    qDebug() << "sport settings recieved" << rigidMetricsSettings;
    rigidBodyMetrics.setMetricSettings(rigidMetricsSettings);
    skeletonMetrics->setMetricSettings(bodyMetricsSettings);
}

std::unordered_map<int, std::string> DataProcessor::getRigidBodyMap() 
{
    return rigidBodyMetrics.getRigidBodyMap();
}

const std::unordered_map<int, std::string>& DataProcessor::getSkeletonNameMap() const
{
    return skeletonMetrics->getSkeletonNameMap();
}

const std::unordered_map<int, std::unordered_map<int, std::string>>& DataProcessor::getBoneNameMap() const
{
    return skeletonMetrics->getBoneNameMap();
}

const std::vector<FrameData>& DataProcessor::getFrames()
{
    return m_frames;
}