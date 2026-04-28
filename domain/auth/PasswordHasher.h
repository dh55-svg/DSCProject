#pragma once
#include <qstring.h>
#include <qcryptographichash.h>
#include <QRandomGenerator>

class PasswordHasher {
public:
    static QString hash(const QString& password) {
        QByteArray salt;
        for (int i = 0; i < 16; ++i) {
            salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
        }
        QString saltHex = QString::fromLatin1(salt.toHex());
        const int iterations = 10000;
        QByteArray work = (saltHex + password).toUtf8();
        for (int i = 0; i < iterations; ++i) {
            work = QCryptographicHash::hash(work, QCryptographicHash::Sha256);
        }
        return QStringLiteral("%1$%2$%3")
            .arg(iterations)
            .arg(saltHex)
            .arg(QString::fromLatin1(work.toHex()));
    }

    static bool verify(const QString& password, const QString& storedHash) {
        if (!storedHash.contains('$')) {
            // Backward compatible: unsalted SHA-256
            QByteArray hash = QCryptographicHash::hash(
                password.toUtf8(), QCryptographicHash::Sha256);
            return QString::fromLatin1(hash.toHex()) == storedHash;
        }
        QStringList parts = storedHash.split('$');
        if (parts.size() != 3) return false;

        int iterations = parts[0].toInt();
        QString salt = parts[1];
        QString expectedHash = parts[2];

        QByteArray work = (salt + password).toUtf8();
        for (int i = 0; i < iterations; ++i) {
            work = QCryptographicHash::hash(work, QCryptographicHash::Sha256);
        }
        return QString::fromLatin1(work.toHex()) == expectedHash;
    }
};
