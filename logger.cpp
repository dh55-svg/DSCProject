#include "logger.h"

static Logger::LogCallback s_logCallback = nullptr;

void Logger::setLogCallback(LogCallback cb)
{
    s_logCallback = cb;
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

void Logger::setLogDir(const QString& dir)
{
    QMutexLocker lock(&m_mutex);
    m_logDir = dir;
    QDir().mkpath(m_logDir);
}

void Logger::setLogLevel(Log_Level level)
{
    m_level = level;
}

void Logger::setLogMaxFileSize(int sizeMb)
{
    QMutexLocker lock(&m_mutex);
    m_maxFileSize = sizeMb;
}

void Logger::setLogRotationCount(int count)
{
    QMutexLocker lock(&m_mutex);
    m_maxRotationCount = count;
}

void Logger::log(Log_Level log, const QString& model, const QString& message)
{
    if (log < m_level)
        return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd HH:mm:ss.zzz");
    QString levelStr = LevelToString(log);
    QString logLine = QString("[%1][%2][%3] : %4").arg(timestamp).arg(levelStr).arg(model).arg(message);

    qDebug().noquote() << logLine;

    {
        QMutexLocker lock(&m_mutex);
        if (!m_currentFile.isOpen())
        {
            if (!openLogFile()) return;
        }

        checkRotation();
        m_stream << logLine << "\n";
        m_stream.flush();
        m_currentSize += logLine.size() + 1;
    }

    if (s_logCallback) {
        s_logCallback(log, model, message);
    }
}

Logger::~Logger()
{
    if (m_currentFile.isOpen())
    {
        m_stream.flush();
        m_currentFile.close();
    }
}

void Logger::checkRotation()
{
    qint64 maxFileSize = static_cast<qint64>(m_maxFileSize) * 1024 * 1024;
    if (m_currentSize < maxFileSize) return;

    m_stream.flush();
    m_currentFile.close();

    QString basename = m_currentFile.fileName();
    for (int i = m_maxRotationCount - 1; i > 0; --i)
    {
        QString oldName = QString("%1.%2").arg(basename).arg(i);
        QString newName = QString("%1.%2").arg(basename).arg(i + 1);
        QFile::remove(newName);
        QFile::rename(oldName, newName);
    }

    QString baseUpName = basename + ".1";
    QFile::rename(basename, baseUpName);

    openLogFile();
}

bool Logger::openLogFile()
{
    QString logFile = QString("doc_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd"));
    QString path = m_logDir + "/" + logFile;

    m_currentFile.setFileName(path);
    if (!m_currentFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
        qWarning() << "打开日志文件失败:" << path;
        return false;
    }
    m_stream.setDevice(&m_currentFile);
    m_currentSize = m_currentFile.size();
    return true;
}

QString Logger::LevelToString(Log_Level level)
{
    switch (level)
    {
    case Log_Level::Debug:   return "Debug";
    case Log_Level::Info:    return "Info";
    case Log_Level::Warning: return "Warning";
    case Log_Level::Error:   return "Error";
    case Log_Level::Fatal:   return "Fatal";
    default:                 return "????";
    }
}
