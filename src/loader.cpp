#include "loader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

Loader::Loader(const QString &cacheDirectory, QObject *parent)
    : QObject(parent),
      m_cacheDirectory(!cacheDirectory.isEmpty() ? cacheDirectory
                       : QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/keystone/models") {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, [this] {
        const QByteArray chunk = m_process.readAll();
        // Keep the tail for the error message; the bar redraws in place.
        m_output = (m_output + chunk).right(64 * 1024);
        if (chunk.contains("Writing geometry") || chunk.contains("writing geometry"))
            m_creatingGeometry = false;
        const double bar = parseProgress(chunk);
        // Creating the geometry is nearly all the work; writing it out is the
        // last stretch.
        if (bar >= 0)
            setProgress(m_creatingGeometry ? bar * 0.85 : 0.85 + bar * 0.1);
    });
    connect(&m_process, &QProcess::finished, this, &Loader::conversionFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_running)
            fail(tr("IfcConvert could not be started: %1").arg(m_process.errorString()));
    });
}

Loader::~Loader() {
    cancel();
}

QString Loader::findIfcConvert() {
    const QString fromEnvironment = qEnvironmentVariable("KEYSTONE_IFCCONVERT");
    if (!fromEnvironment.isEmpty())
        return QFileInfo(fromEnvironment).isExecutable() ? fromEnvironment : QString();
    const QString here = QCoreApplication::applicationDirPath();
    for (const QString &candidate : {here + "/../lib/keystone/ifcconvert/IfcConvert",
                                     here + "/../vendor/ifcconvert/IfcConvert",
                                     here + "/../../vendor/ifcconvert/IfcConvert",
                                     QStringLiteral("/usr/lib/keystone/ifcconvert/IfcConvert")}) {
        if (QFileInfo(candidate).isExecutable())
            return QFileInfo(candidate).canonicalFilePath();
    }
    for (const QString &name : {QStringLiteral("IfcConvert"), QStringLiteral("ifcconvert")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    return QString();
}

QStringList Loader::convertArguments(const QString &input, const QString &output) {
    // Groups named by GlobalId tie each mesh back to its IFC element, and
    // world coordinates put every element where the building has it.
    // Openings, spaces, and spatial zones are volumes rather than things, and
    // drawn they would wrap the building in boxes that catch every click.
    return {QStringLiteral("--use-element-guids"),
            QStringLiteral("--use-world-coords"),
            QStringLiteral("-j"), QString::number(qMax(1, QThread::idealThreadCount())),
            QStringLiteral("-y"),
            QStringLiteral("--exclude"), QStringLiteral("entities"),
            QStringLiteral("IfcOpeningElement"), QStringLiteral("IfcSpace"), QStringLiteral("IfcSpatialZone"),
            QStringLiteral("--"),
            input, output};
}

double Loader::parseProgress(const QByteArray &output) {
    const qsizetype open = output.lastIndexOf('[');
    if (open < 0)
        return -1;
    qsizetype close = output.indexOf(']', open);
    const QByteArray bar = output.mid(open + 1, close < 0 ? -1 : close - open - 1);
    if (bar.isEmpty() || bar.count('#') + bar.count(' ') != bar.size())
        return -1;
    // A bar is 50 wide; a partial redraw still counts its marks out of 50.
    return qBound(0.0, double(bar.count('#')) / 50.0, 1.0);
}

QString Loader::cacheKey(const QString &path, const QString &ifcConvert) const {
    // A different file, a changed file, or a different IfcConvert each make
    // a new entry; the arguments' version keeps old entries from being used
    // after a change to how models are converted.
    const QFileInfo model(path), tool(ifcConvert);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArrayLiteral("keystone-obj-1\n"));
    hash.addData(model.absoluteFilePath().toUtf8());
    hash.addData(QByteArray::number(model.size()));
    hash.addData(QByteArray::number(model.lastModified().toMSecsSinceEpoch()));
    hash.addData(tool.canonicalFilePath().toUtf8());
    hash.addData(QByteArray::number(tool.lastModified().toMSecsSinceEpoch()));
    hash.addData(convertArguments(QString(), QString()).join(' ').toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

void Loader::setProgress(double progress) {
    if (qFuzzyCompare(progress + 1, m_progress + 1))
        return;
    m_progress = progress;
    emit progressChanged();
}

void Loader::pruneCache() {
    // Newest first by when each model was last opened, which touches its OBJ.
    QFileInfoList entries;
    for (const QFileInfo &entry : QDir(m_cacheDirectory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        entries.append(QFileInfo(entry.absoluteFilePath() + "/model.obj"));
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    for (int i = cacheSize; i < entries.size(); ++i)
        QDir(entries.at(i).absolutePath()).removeRecursively();
    // Work folders a crash left behind.
    for (const QFileInfo &work : QDir(m_cacheDirectory).entryInfoList({".convert-*"}, QDir::Dirs | QDir::Hidden))
        if (work.lastModified() < QDateTime::currentDateTime().addDays(-1))
            QDir(work.absoluteFilePath()).removeRecursively();
}

void Loader::start(const QString &path) {
    cancel();
    const int generation = ++m_generation;
    m_running = true;
    m_path = path;
    m_infoDone = m_meshDone = false;
    m_output.clear();
    m_creatingGeometry = true;
    m_progress = 0;
    emit progressChanged();

    const QString ifcConvert = findIfcConvert();
    if (ifcConvert.isEmpty()) {
        fail(tr("Keystone needs IfcConvert from IfcOpenShell to read IFC geometry, and it isn't installed. "
                "Reinstall Keystone, or point KEYSTONE_IFCCONVERT at an IfcConvert binary."));
        return;
    }
    m_infoWatcher.disconnect(this);
    connect(&m_infoWatcher, &QFutureWatcher<Parsed<IfcInfo>>::finished, this, [this, generation] {
        if (generation != m_generation)
            return;
        m_infoDone = true;
        maybeFinish();
    });
    m_infoWatcher.setFuture(QtConcurrent::run([path]() {
        Parsed<IfcInfo> parsed{std::make_shared<IfcInfo>(), QString()};
        if (!parsed.value->load(path, &parsed.error))
            parsed.value.reset();
        return parsed;
    }));

    m_cacheEntry = QDir(m_cacheDirectory).filePath(cacheKey(path, ifcConvert));
    const QString cached = m_cacheEntry + "/model.obj";
    if (QFileInfo::exists(cached)) {
        // Touched, so the cache keeps what's opened most recently.
        QFile touch(cached);
        if (touch.open(QIODevice::ReadWrite))
            touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        setProgress(0.95);
        readMesh(cached);
        return;
    }

    // The OBJ lands in the cache, never beside the user's file.
    if (!QDir().mkpath(m_cacheDirectory)) {
        fail(tr("Could not create the cache folder %1.").arg(m_cacheDirectory));
        return;
    }
    m_work = std::make_shared<QTemporaryDir>(QDir(m_cacheDirectory).filePath(".convert-XXXXXX"));
    if (!m_work->isValid()) {
        fail(tr("Could not create a folder to convert the model in: %1").arg(m_work->errorString()));
        return;
    }
    m_process.setWorkingDirectory(m_work->path());
    m_process.start(ifcConvert, convertArguments(path, m_work->filePath("model.obj")));
}

void Loader::readMesh(const QString &objPath, std::shared_ptr<QTemporaryDir> keep) {
    const int generation = m_generation;
    m_meshWatcher.disconnect(this);
    connect(&m_meshWatcher, &QFutureWatcher<Parsed<Mesh>>::finished, this, [this, generation] {
        if (generation != m_generation)
            return;
        m_meshDone = true;
        maybeFinish();
    });
    m_meshWatcher.setFuture(QtConcurrent::run([objPath, keep]() {
        Parsed<Mesh> parsed{std::make_shared<Mesh>(), QString()};
        if (!readObj(objPath, parsed.value.get(), &parsed.error))
            parsed.value.reset();
        return parsed;
    }));
}

void Loader::conversionFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_running)
        return;
    m_output = (m_output + m_process.readAll()).right(64 * 1024);
    const QString output = QString::fromUtf8(m_output);
    if (status != QProcess::NormalExit) {
        fail(tr("IfcConvert stopped unexpectedly while reading %1.").arg(QFileInfo(m_path).fileName()));
        return;
    }
    QString objPath = m_work->filePath("model.obj");
    if (exitCode != 0 || !QFileInfo::exists(objPath)) {
        // The last error IfcConvert logged says the most.
        QString reason;
        for (const QString &line : output.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts))
            if (line.contains("error", Qt::CaseInsensitive))
                reason = line.trimmed();
        fail(reason.isEmpty() ? tr("IfcConvert could not read %1.").arg(QFileInfo(m_path).fileName())
                              : tr("IfcConvert could not read %1: %2").arg(QFileInfo(m_path).fileName(), reason));
        return;
    }

    // Finished, so it becomes a cache entry in one rename; if that fails the
    // model still opens from where it is and isn't cached.
    QDir(m_cacheEntry).removeRecursively();
    if (QDir().rename(m_work->path(), m_cacheEntry)) {
        m_work->setAutoRemove(false);
        m_work.reset();
        objPath = m_cacheEntry + "/model.obj";
        pruneCache();
    }
    setProgress(0.95);
    readMesh(objPath, m_work);
}

void Loader::maybeFinish() {
    if (!m_running || !m_infoDone || !m_meshDone)
        return;
    const auto meshResult = m_meshWatcher.result();
    const auto infoResult = m_infoWatcher.result();
    const auto mesh = meshResult.value;
    const auto info = infoResult.value;
    const QString name = QFileInfo(m_path).fileName();
    if (!mesh) {
        fail(tr("Could not read the geometry IfcConvert made from %1: %2").arg(name, meshResult.error));
        return;
    }
    if (mesh->isEmpty()) {
        fail(tr("%1 has no 3D geometry to show.").arg(name));
        return;
    }

    Result result;
    result.mesh = mesh;
    result.info = info;
    result.path = m_path;
    if (!info)
        result.warning = tr("Properties could not be read from %1: %2").arg(name, infoResult.error);
    m_running = false;
    m_work.reset();
    setProgress(1);
    emit finished(result);
}

void Loader::fail(const QString &message) {
    m_running = false;
    ++m_generation;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    m_work.reset();
    emit failed(message);
}

void Loader::cancel() {
    ++m_generation;
    m_running = false;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    // Workers still reading carry on in the background rather than holding
    // up the window: they share nothing with the loader, the mesh worker
    // keeps its own hold on the work folder, and the generation check drops
    // what they find.
    m_infoWatcher.disconnect(this);
    m_meshWatcher.disconnect(this);
    m_work.reset();
}
