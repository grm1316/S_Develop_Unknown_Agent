// ClientUtils.cpp - 슬림화된 유틸리티 구현
// 콘솔 출력, JSON 검증, 공통 유틸리티만 구현

#include "ClientUtils.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLocale>
#include <QTimeZone>
#include <iostream>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

using namespace ClientUtilsConstants;

// =================================================================
// 콘솔 출력 함수들
// =================================================================

void ClientUtils::showErrorMessage(const QString& title, const QString& message) {
#ifdef _WIN32
    // Windows API 직접 사용 (한글 지원)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, COLOR_RED);

    QString output = QString("[ERROR] %1: %2\n").arg(title, message);

    // UTF-16으로 변환하여 출력
    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);

    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
#else
    // Linux/Mac은 기존 방식
    std::cout << "[ERROR] " << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
}

void ClientUtils::showInfoMessage(const QString& title, const QString& message) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, COLOR_BLUE);

    QString output = QString("[INFO] %1: %2\n").arg(title, message);
    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);

    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
#else
    std::cout << "[INFO] " << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
}

void ClientUtils::showSuccessMessage(const QString& title, const QString& message) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, COLOR_GREEN);

    QString output = QString("[SUCCESS] %1: %2\n").arg(title, message);
    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);

    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
#else
    std::cout << "[SUCCESS] " << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
}

void ClientUtils::showWarningMessage(const QString& title, const QString& message) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, COLOR_YELLOW);

    QString output = QString("[WARNING] %1: %2\n").arg(title, message);
    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);

    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
#else
    std::cout << "[WARNING] " << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
}

void ClientUtils::showProgress(const QString& message, int progress) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    QString output;
    if (progress >= 0 && progress <= 100) {
        output = QString("[PROGRESS] %1 (%2%%)\n").arg(message).arg(progress);
    } else {
        output = QString("[PROGRESS] %1...\n").arg(message);
    }

    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);
#else
    if (progress >= 0 && progress <= 100) {
        std::cout << "[PROGRESS] " << message.toStdString() << " (" << progress << "%)" << std::endl;
    } else {
        std::cout << "[PROGRESS] " << message.toStdString() << "..." << std::endl;
    }
#endif
}

void ClientUtils::clearConsole() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// =================================================================
// JSON 유효성 검사 함수들
// =================================================================

bool ClientUtils::validateJsonNumbers(const QJsonObject& json, const QString& path) {
    for (auto it = json.begin(); it != json.end(); ++it) {
        QString currentPath = path.isEmpty() ? it.key() : path + "." + it.key();

        QJsonValue value = it.value();

        if (value.isDouble()) {
            if (!isValidNumber(value)) {
                qCritical() << QString("[JSON Validation] Invalid number at %1: %2")
                .arg(currentPath).arg(value.toDouble());
                return false;
            }
        } else if (value.isObject()) {
            if (!validateJsonNumbers(value.toObject(), currentPath)) {
                return false;
            }
        } else if (value.isArray()) {
            if (!validateJsonArrayNumbers(value.toArray(), currentPath)) {
                return false;
            }
        }
    }
    return true;
}

bool ClientUtils::validateJsonData(const QJsonObject& jsonData) {
    if (jsonData.isEmpty()) {
        qWarning() << "[JSON Validation] Empty JSON object";
        return false;
    }

    // 숫자 유효성 검사
    if (!validateJsonNumbers(jsonData)) {
        return false;
    }

    // 기본 구조 검사 (collection_info가 있는지)
    if (jsonData.contains("collection_info")) {
        QJsonObject collectionInfo = jsonData.value("collection_info").toObject();
        if (collectionInfo.isEmpty()) {
            qWarning() << "[JSON Validation] Empty collection_info";
            return false;
        }

        // 필수 필드 검사
        QStringList requiredFields = {"module_name", "collection_time"};
        for (const QString& field : requiredFields) {
            if (!collectionInfo.contains(field)) {
                qWarning() << QString("[JSON Validation] Missing required field: %1").arg(field);
                return false;
            }
        }
    }

    qDebug() << "[JSON Validation] JSON data validation passed";
    return true;
}

bool ClientUtils::validateJsonArrayNumbers(const QJsonArray& jsonArray, const QString& path) {
    for (int i = 0; i < jsonArray.size(); ++i) {
        QString arrayPath = path + QString("[%1]").arg(i);
        QJsonValue value = jsonArray[i];

        if (value.isDouble()) {
            if (!isValidNumber(value)) {
                qCritical() << QString("[JSON Validation] Invalid number at %1: %2")
                .arg(arrayPath).arg(value.toDouble());
                return false;
            }
        } else if (value.isObject()) {
            if (!validateJsonNumbers(value.toObject(), arrayPath)) {
                return false;
            }
        } else if (value.isArray()) {
            if (!validateJsonArrayNumbers(value.toArray(), arrayPath)) {
                return false;
            }
        }
    }
    return true;
}

// =================================================================
// 공통 문자열 처리 유틸리티
// =================================================================

QString ClientUtils::getClientDisplayName() {
    QString folderName = getCurrentFolderName();

    // 특수문자를 언더스코어로 변경 (데이터베이스 호환성)
    folderName.replace(QRegularExpression("[^a-zA-Z0-9가-힣_-]"), "_");

    // 길이 제한 (50자)
    if (folderName.length() > 50) {
        folderName = folderName.left(50);
    }

    return folderName;
}

QString ClientUtils::getCurrentFolderName() {
    QString appDirPath = QCoreApplication::applicationDirPath();
    QDir appDir(appDirPath);
    QString folderName = appDir.dirName();

    if (folderName.isEmpty() || folderName == "." || folderName == "..") {
        return "Unknown_Client";
    }

    return folderName;
}

QString ClientUtils::formatFileSize(qint64 bytes) {
    if (bytes < 0) return "0 B";

    if (bytes < KB) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < MB) {
        return QString("%1 KB").arg(QString::number(bytes / static_cast<double>(KB), 'f', 1));
    } else if (bytes < GB) {
        return QString("%1 MB").arg(QString::number(bytes / static_cast<double>(MB), 'f', 1));
    } else {
        return QString("%1 GB").arg(QString::number(bytes / static_cast<double>(GB), 'f', 2));
    }
}

QString ClientUtils::formatTimeInterval(const QDateTime& startTime, const QDateTime& endTime) {
    if (!startTime.isValid() || !endTime.isValid()) {
        return "Invalid time";
    }

    qint64 msecs = startTime.msecsTo(endTime);

    if (msecs < 0) {
        return "0ms";
    } else if (msecs < 1000) {
        return QString("%1ms").arg(msecs);
    } else if (msecs < 60000) {
        return QString("%1s").arg(QString::number(msecs / 1000.0, 'f', 1));
    } else {
        int minutes = static_cast<int>(msecs / 60000);
        int seconds = static_cast<int>((msecs % 60000) / 1000);
        return QString("%1m %2s").arg(minutes).arg(seconds);
    }
}

QString ClientUtils::createSafeFileName(const QString& fileName) {
    QString safeName = fileName;

    // Windows에서 사용 불가능한 문자들 제거
    QRegularExpression invalidChars(R"([<>:"/\\|?*])");
    safeName.replace(invalidChars, "_");

    // 연속된 언더스코어 제거
    safeName.replace(QRegularExpression("_+"), "_");

    // 앞뒤 공백 및 언더스코어 제거
    safeName = safeName.trimmed();
    if (safeName.startsWith("_")) safeName.remove(0, 1);
    if (safeName.endsWith("_")) safeName.chop(1);

    // 길이 제한
    if (safeName.length() > MAX_FILENAME_LENGTH) {
        safeName = safeName.left(MAX_FILENAME_LENGTH);
    }

    // 빈 이름 처리
    if (safeName.isEmpty()) {
        safeName = "unnamed_file";
    }

    return safeName;
}

// =================================================================
// JSON 파일 저장 유틸리티
// =================================================================

bool ClientUtils::saveJsonToFile(const QJsonObject& jsonObject, const QString& filePath, bool indent) {
    if (jsonObject.isEmpty()) {
        qWarning() << "[ClientUtils] Empty JSON object, cannot save";
        return false;
    }

    if (filePath.isEmpty()) {
        qWarning() << "[ClientUtils] Empty file path, cannot save";
        return false;
    }

    // 디렉토리 생성 (필요한 경우)
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "[ClientUtils] Failed to create directory:" << dir.absolutePath();
            return false;
        }
    }

    // JSON 유효성 검사
    if (!validateJsonData(jsonObject)) {
        qWarning() << "[ClientUtils] JSON validation failed, saving anyway";
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ClientUtils] Failed to open file for writing:" << filePath;
        return false;
    }

    QJsonDocument doc(jsonObject);
    QByteArray jsonData = indent ? doc.toJson(QJsonDocument::Indented) : doc.toJson(QJsonDocument::Compact);

    qint64 written = file.write(jsonData);
    file.close();

    if (written == jsonData.size()) {
        qDebug() << QString("[ClientUtils] JSON saved successfully: %1 (%2)")
        .arg(filePath).arg(formatFileSize(written));
        return true;
    } else {
        qWarning() << QString("[ClientUtils] Partial write: %1/%2 bytes").arg(written).arg(jsonData.size());
        return false;
    }
}

QJsonObject ClientUtils::loadJsonFromFile(const QString& filePath) {
    QJsonObject result;

    if (filePath.isEmpty()) {
        qWarning() << "[ClientUtils] Empty file path";
        return result;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qWarning() << "[ClientUtils] File does not exist:" << filePath;
        return result;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ClientUtils] Failed to open file for reading:" << filePath;
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        qWarning() << "[ClientUtils] Empty file:" << filePath;
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << QString("[ClientUtils] JSON parse error: %1 at offset %2")
        .arg(parseError.errorString()).arg(parseError.offset);
        return result;
    }

    if (!doc.isObject()) {
        qWarning() << "[ClientUtils] JSON is not an object";
        return result;
    }

    result = doc.object();
    qDebug() << QString("[ClientUtils] JSON loaded successfully: %1 (%2)")
                    .arg(filePath).arg(formatFileSize(data.size()));

    return result;
}

qint64 ClientUtils::calculateJsonSize(const QJsonObject& jsonObject, bool compact) {
    if (jsonObject.isEmpty()) return 0;

    QJsonDocument doc(jsonObject);
    QByteArray data = compact ? doc.toJson(QJsonDocument::Compact) : doc.toJson(QJsonDocument::Indented);
    return data.size();
}

// =================================================================
// 시스템 정보 조회
// =================================================================

QString ClientUtils::getCurrentLanguage() {
    QLocale locale = QLocale::system();
    return locale.bcp47Name().split('-').first(); // "ko-KR" -> "ko"
}

QString ClientUtils::getCurrentTimeZone() {
    QTimeZone timeZone = QTimeZone::systemTimeZone();
    return timeZone.id();
}

QString ClientUtils::getApplicationVersion() {
    return "2.0.0"; // 하드코딩된 버전 (필요시 설정 파일에서 읽기)
}

// =================================================================
// 내부 헬퍼 함수들
// =================================================================

void ClientUtils::setConsoleColor(int color) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, static_cast<WORD>(color | FOREGROUND_INTENSITY));
    }
#else
    // Linux/Mac의 경우 ANSI 색상 코드 사용
    switch (color) {
    case COLOR_RED:
        std::cout << "\033[31m";
        break;
    case COLOR_GREEN:
        std::cout << "\033[32m";
        break;
    case COLOR_BLUE:
        std::cout << "\033[34m";
        break;
    case COLOR_YELLOW:
        std::cout << "\033[33m";
        break;
    default:
        std::cout << "\033[0m";
        break;
    }
#endif
}

void ClientUtils::resetConsoleColor() {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
    }
#else
    std::cout << "\033[0m";
#endif
}

bool ClientUtils::isValidNumber(const QJsonValue& value) {
    if (!value.isDouble()) return true; // 숫자가 아니면 유효

    double num = value.toDouble();

    // NaN 검사
    if (std::isnan(num)) {
        return false;
    }

    // Infinity 검사
    if (std::isinf(num)) {
        return false;
    }

    return true;
}
