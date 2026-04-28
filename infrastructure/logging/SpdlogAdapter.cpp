#include "SpdlogAdapter.h"
#include <qdebug.h>

SpdlogAdapter::SpdlogAdapter() {
    QDir().mkpath(m_logDir);
}

SpdlogAdapter::~SpdlogAdapter() {
    if (m_currentFile.isOpen()) {
        m_stream.flush();
        m_currentFile.close();
    }
}

void SpdlogAdapter::setLogDir(const QString& dir) {
    QMutexLocker lock(&m_mutex);
    m_logDir = dir;
    QDir().mkpath(m_logDir);
}

void SpdlogAdapter::log(LogLevel level, const QString& msg) {
    if (level < m_level) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);
    QString logLine = QString("[%1][%2] %3").arg(timestamp, levelStr, msg);

    qDebug().noquote() << logLine;

    {
        QMutexLocker lock(&m_mutex);
        if (!m_currentFile.isOpen()) {
            if (!openLogFile()) return;
        }
        checkRotation();
        m_stream << logLine << "\n";
        m_stream.flush();
        m_currentSize += logLine.size() + 1;
    }

    if (m_callback) {
        m_callback(logLine);
    }
}

void SpdlogAdapter::checkRotation() {
    qint64 maxFileSize = static_cast<qint64>(m_maxFileSize) * 1024 * 1024;
    if (m_currentSize < maxFileSize) return;

    m_stream.flush();
    m_currentFile.close();

    QString basename = m_currentFile.fileName();
    for (int i = m_maxRotationCount - 1; i > 0; --i) {
        QString oldName = QString("%1.%2").arg(basename).arg(i);
        QString newName = QString("%1.%2").arg(basename).arg(i + 1);
        QFile::remove(newName);
        QFile::rename(oldName, newName);
    }
    QFile::rename(basename, basename + ".1");
    openLogFile();
}

bool SpdlogAdapter::openLogFile() {
    QString logFile = QString("doc_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd"));
    QString path = m_logDir + "/" + logFile;
    m_currentFile.setFileName(path);
    if (!m_currentFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qWarning() << "Failed to open log file:" << path;
        return false;
    }
    m_stream.setDevice(&m_currentFile);
    m_currentSize = m_currentFile.size();
    return true;
}

QString SpdlogAdapter::levelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "Debug";
    case LogLevel::Info:  return "Info";
    case LogLevel::Warn:  return "Warning";
    case LogLevel::Error: return "Error";
    case LogLevel::Fatal: return "Fatal";
    default:              return "????";
    }
}
