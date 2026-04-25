#pragma once
#include "core_global.h"
#include <qobject.h>
#include <QTimer>
#include <QDateTime>
#include <QString>
#include <QJsonObject>

class CORE_EXPORT DataBackupManager :public QObject {
	Q_OBJECT
public:
	enum class BackupType {
		Full, // full backup
		Incremental// incremental backup
	};
	Q_ENUM(BackupType)


private:
	DataBackupManager();
	~DataBackupManager() override;
	DataBackupManager(const DataBackupManager&) = delete;
	DataBackupManager& operator=(const DataBackupManager&) = delete;

	bool createFullBackup();
	bool createIncrementalBackup();
	bool cleanupOldBackups();
	QString generateBackupFileName(BackupType type) const;
	bool verifyBackupIntegrity(const QString& backupPath) const;
};
