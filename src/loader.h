#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

#include "ifcinfo.h"
#include "mesh.h"

// Turns an .ifc file into a Mesh and an IfcInfo without blocking the window:
// IfcConvert (IfcOpenShell) triangulates the geometry into an OBJ while the
// STEP text is indexed on a worker thread, then the OBJ is read on one too.
// Triangulating is most of the wait on a real building, so the OBJ is kept in
// a small cache, keyed by the file's path, size, and time, and opening the
// same model again skips it. The user's file is only ever read.
class Loader : public QObject {
    Q_OBJECT

public:
    struct Result {
        std::shared_ptr<const Mesh> mesh;
        std::shared_ptr<const IfcInfo> info;
        // Something the user should know that didn't stop the model opening.
        QString warning;
    };

    // The cache defaults to ~/.cache/keystone/models; tests pass their own.
    explicit Loader(const QString &cacheDirectory = {}, QObject *parent = nullptr);
    ~Loader() override;

    // IfcConvert from $KEYSTONE_IFCCONVERT, beside the installed binary, the
    // development vendor/ directory, or PATH; empty when there is none.
    static QString findIfcConvert();
    // The IfcConvert arguments for one conversion, for tests and the README.
    static QStringList convertArguments(const QString &input, const QString &output);
    // How far an IfcConvert progress bar ("\r[#####     ]") has got, 0 to 1,
    // or -1 when the text holds none.
    static double parseProgress(const QByteArray &output);
    QString cacheDirectory() const { return m_cacheDirectory; }
    // How many converted models the cache keeps, newest first.
    static constexpr int cacheSize = 12;

    void start(const QString &path);
    void cancel();
    bool isRunning() const { return m_running; }
    double progress() const { return m_progress; }

signals:
    void finished(const Loader::Result &result);
    void failed(const QString &message);
    void progressChanged();

private:
    QString cacheKey(const QString &path, const QString &ifcConvert) const;
    void readMesh(const QString &objPath);
    void pruneCache();
    void setProgress(double progress);
    void conversionFinished(int exitCode, QProcess::ExitStatus status);
    void maybeFinish();
    void fail(const QString &message);

    bool m_running = false;
    double m_progress = 0;
    QString m_path;
    QString m_cacheDirectory;
    QString m_cacheEntry;
    // Where IfcConvert writes, inside the cache, renamed to the entry once
    // it has finished so a half-written model is never taken for a cached one.
    std::unique_ptr<QTemporaryDir> m_work;
    QByteArray m_output;
    bool m_creatingGeometry = true;
    QProcess m_process;
    template <typename T> struct Parsed {
        std::shared_ptr<T> value;
        QString error;
    };
    QFutureWatcher<Parsed<IfcInfo>> m_infoWatcher;
    QFutureWatcher<Parsed<Mesh>> m_meshWatcher;
    bool m_infoDone = false;
    bool m_meshDone = false;
    // Bumped per start(), so a late result from a cancelled load is ignored.
    int m_generation = 0;
};
