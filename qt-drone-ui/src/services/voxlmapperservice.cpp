#include "voxlmapperservice.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QDebug>

// ── MPA pipe paths on the VOXL 2 ──────────────────────────────────────────
static const char *kMeshControlPipe    = "/run/mpa/voxl_mesh/control";
static const char *kPlannerControlPipe = "/run/mpa/voxl_planner/control";

// ── voxl_mapper.cc control command strings ────────────────────────────────
static const char *kCmdResetVio      = "reset_vio";
static const char *kCmdClearMap      = "clear_map";
static const char *kCmdFollowPath    = "follow_path";
static const char *kCmdAbortPath     = "abort";
static const char *kCmdClearPaths    = "clear_paths";
static const char *kCmdPlanHome      = "plan_home";

VoxlMapperService::VoxlMapperService(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

VoxlMapperService::~VoxlMapperService()
{
    if (m_sshProcess) {
        m_sshProcess->kill();
        m_sshProcess->deleteLater();
    }
    if (m_meshReply) {
        m_meshReply->abort();
    }
}

void VoxlMapperService::setHost(const QString &host)
{
    m_host = host;
}

// ── Mesh download ──────────────────────────────────────────────────────────

void VoxlMapperService::fetchMesh(MeshFormat format)
{
    if (m_host.isEmpty()) {
        emit meshFetchFailed("No VOXL host configured");
        return;
    }

    if (m_meshReply) {
        m_meshReply->abort();
        m_meshReply->deleteLater();
        m_meshReply = nullptr;
    }

    m_pendingMeshFormat = format;
    QString urlStr = QString("http://%1:%2/mesh_api/%3")
                         .arg(m_host)
                         .arg(m_portalPort)
                         .arg(meshFormatString(format));

    QNetworkRequest request{QUrl(urlStr)};
    request.setTransferTimeout(30000);

    m_meshReply = m_networkManager->get(request);
    connect(m_meshReply, &QNetworkReply::finished, this, &VoxlMapperService::onMeshReplyFinished);

    emit statusChanged(QString("Fetching %1 mesh from %2…")
                           .arg(meshFormatString(format).toUpper(), m_host));
}

void VoxlMapperService::onMeshReplyFinished()
{
    if (!m_meshReply) return;

    if (m_meshReply->error() == QNetworkReply::NoError) {
        QByteArray data = m_meshReply->readAll();
        emit meshReceived(data, m_pendingMeshFormat);
        emit statusChanged(QString("Mesh received (%1 bytes)").arg(data.size()));
    } else {
        emit meshFetchFailed(m_meshReply->errorString());
    }

    m_meshReply->deleteLater();
    m_meshReply = nullptr;
}

// ── Map control ────────────────────────────────────────────────────────────

void VoxlMapperService::saveMap(const QString &dir, MeshFormat format)
{
    // Command format: "save_map:<format>:<dir>"  (dir may be empty)
    QString cmd = QString("save_map:%1").arg(meshFormatString(format));
    if (!dir.isEmpty()) {
        cmd += QString(":%1").arg(dir);
    }
    sendPipeCommand(kMeshControlPipe, cmd);
}

void VoxlMapperService::loadMap(const QString &dir)
{
    QString cmd = "load_map";
    if (!dir.isEmpty()) {
        cmd += QString(":%1").arg(dir);
    }
    sendPipeCommand(kMeshControlPipe, cmd);
}

void VoxlMapperService::clearMap()
{
    sendPipeCommand(kMeshControlPipe, kCmdClearMap);
}

void VoxlMapperService::resetVio()
{
    sendPipeCommand(kMeshControlPipe, kCmdResetVio);
}

void VoxlMapperService::setSliceLevel(double level)
{
    // Command format: "slice_level:<value>"
    sendPipeCommand(kMeshControlPipe,
                    QString("slice_level:%1").arg(level, 0, 'f', 1));
}

void VoxlMapperService::setMeshColorMap(const QString &colorMap)
{
    sendPipeCommand(kMeshControlPipe,
                    QString("mesh_color_map:%1").arg(colorMap));
}

void VoxlMapperService::setMeshColorMode(const QString &colorMode)
{
    sendPipeCommand(kMeshControlPipe,
                    QString("mesh_color_mode:%1").arg(colorMode));
}

// ── Path planning ──────────────────────────────────────────────────────────

void VoxlMapperService::planToPoint(float x, float y, float z)
{
    // Command format expected by the planner control pipe: "plan_to:x:y:z"
    QString cmd = QString("plan_to:%1:%2:%3")
                      .arg(x, 0, 'f', 4)
                      .arg(y, 0, 'f', 4)
                      .arg(z, 0, 'f', 4);
    sendPipeCommand(kPlannerControlPipe, cmd);
}

void VoxlMapperService::planToHome()
{
    sendPipeCommand(kPlannerControlPipe, kCmdPlanHome);
}

void VoxlMapperService::followPath()
{
    sendPipeCommand(kPlannerControlPipe, kCmdFollowPath);
}

void VoxlMapperService::abortPath()
{
    sendPipeCommand(kPlannerControlPipe, kCmdAbortPath);
}

void VoxlMapperService::clearPaths()
{
    sendPipeCommand(kPlannerControlPipe, kCmdClearPaths);
}

// ── SSH helper ─────────────────────────────────────────────────────────────

void VoxlMapperService::sendPipeCommand(const QString &pipe, const QString &command)
{
    if (m_host.isEmpty()) {
        emit commandFailed(command, "No VOXL host configured");
        return;
    }

    // Write via: printf '%s' '<command>' > <pipe>
    // Using printf rather than echo to avoid a trailing newline that could
    // confuse the pipe parser.
    QString remoteCmd = QString("printf '%%s' '%1' > %2").arg(command, pipe);
    runSshCommand(remoteCmd, command);
}

void VoxlMapperService::runSshCommand(const QString &remoteCmd, const QString &commandLabel)
{
    m_sshQueue.append({remoteCmd, commandLabel});
    if (!m_sshBusy) {
        dispatchNextSsh();
    }
}

void VoxlMapperService::dispatchNextSsh()
{
    if (m_sshQueue.isEmpty()) {
        m_sshBusy = false;
        return;
    }

    m_sshBusy = true;
    SshCmd job = m_sshQueue.takeFirst();

    if (m_sshProcess) {
        m_sshProcess->kill();
        m_sshProcess->deleteLater();
    }

    m_sshProcess = new QProcess(this);
    m_sshProcess->setProperty("label", job.label);

    connect(m_sshProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoxlMapperService::onSshProcessFinished);
    connect(m_sshProcess, &QProcess::errorOccurred,
            this, &VoxlMapperService::onSshProcessError);

    QStringList args;
    if (!m_sshPassword.isEmpty()) {
        // Use sshpass when a password is configured
        args << "-p" << m_sshPassword
             << "ssh"
             << "-o" << "StrictHostKeyChecking=no"
             << "-o" << "UserKnownHostsFile=/dev/null"
             << QString("root@%1").arg(m_host)
             << job.remoteCmd;
        m_sshProcess->start("sshpass", args);
    } else {
        args << "-o" << "StrictHostKeyChecking=no"
             << "-o" << "UserKnownHostsFile=/dev/null"
             << QString("root@%1").arg(m_host)
             << job.remoteCmd;
        m_sshProcess->start("ssh", args);
    }

    emit commandSent(job.label);
    emit statusChanged(QString("Sending mapper command: %1").arg(job.label));
}

void VoxlMapperService::onSshProcessFinished(int exitCode, QProcess::ExitStatus)
{
    QString label = m_sshProcess ? m_sshProcess->property("label").toString() : "";

    if (exitCode == 0) {
        emit statusChanged(QString("Mapper command succeeded: %1").arg(label));
    } else {
        QString stderr;
        if (m_sshProcess) {
            stderr = QString::fromLocal8Bit(m_sshProcess->readAllStandardError());
        }
        emit commandFailed(label, QString("exit %1: %2").arg(exitCode).arg(stderr));
    }

    if (m_sshProcess) {
        m_sshProcess->deleteLater();
        m_sshProcess = nullptr;
    }

    dispatchNextSsh();
}

void VoxlMapperService::onSshProcessError(QProcess::ProcessError error)
{
    QString label = m_sshProcess ? m_sshProcess->property("label").toString() : "";
    QString msg;
    switch (error) {
    case QProcess::FailedToStart: msg = "sshpass/ssh not found or not executable"; break;
    case QProcess::Crashed:       msg = "SSH process crashed";                      break;
    default:                      msg = "SSH process error";                        break;
    }
    emit commandFailed(label, msg);

    if (m_sshProcess) {
        m_sshProcess->deleteLater();
        m_sshProcess = nullptr;
    }

    dispatchNextSsh();
}

// ── Utilities ──────────────────────────────────────────────────────────────

QString VoxlMapperService::meshFormatString(MeshFormat format) const
{
    switch (format) {
    case OBJ:  return "obj";
    case GLTF: return "gltf";
    case PLY:
    default:   return "ply";
    }
}
