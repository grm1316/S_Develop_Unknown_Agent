// ClientServiceManager.cpp - 간소화된 백그라운드 서비스 관리 구현
#include "ClientServiceManager.h"

ClientServiceManager::ClientServiceManager(QObject* parent)
    : QObject(parent)
    , currentMode_(MODE_CONSOLE)
    , backgroundPid_(0)
    , backgroundProcess_(nullptr)
    , processMonitor_(nullptr)
    , consoleHidden_(false)
{
    setupProcessMonitoring();
    qDebug() << "[ServiceManager] Initialized in console mode";
}

ClientServiceManager::~ClientServiceManager() {
    cleanupProcessMonitoring();
    if (backgroundProcess_) {
        stopBackground();
    }
}

// ================================================================
// 실행 모드 관리
// ================================================================

bool ClientServiceManager::setRunMode(RunMode mode) {
    if (currentMode_ == mode) {
        return true;
    }

    qDebug() << QString("[ServiceManager] Changing run mode from %1 to %2")
                    .arg(static_cast<int>(currentMode_))
                    .arg(static_cast<int>(mode));

    switch (mode) {
    case MODE_CONSOLE:
        showConsole();
        break;

    case MODE_BACKGROUND:
        if (!startBackground()) {
            qWarning() << "[ServiceManager] Failed to start background mode";
            return false;
        }
        break;

    case MODE_HIDDEN:
        hideConsole();
        break;
    }

    currentMode_ = mode;
    emit runModeChanged(mode);

    qInfo() << QString("[ServiceManager] Run mode changed to %1").arg(static_cast<int>(mode));
    return true;
}

bool ClientServiceManager::isRunningInBackground() const {
    return (currentMode_ == MODE_BACKGROUND || currentMode_ == MODE_HIDDEN);
}

// ================================================================
// 백그라운드 실행
// ================================================================

bool ClientServiceManager::startBackground() {
    if (backgroundProcess_ && backgroundProcess_->state() == QProcess::Running) {
        qInfo() << "[ServiceManager] Background process already running";
        return true;
    }

    qDebug() << "[ServiceManager] Starting background process...";

    QString executable = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments << "--background" << "--no-console";

    return createBackgroundProcess(arguments);
}

bool ClientServiceManager::stopBackground() {
    if (!backgroundProcess_ || backgroundProcess_->state() != QProcess::Running) {
        qDebug() << "[ServiceManager] No background process to stop";
        return true;
    }

    qDebug() << "[ServiceManager] Stopping background process...";

    backgroundProcess_->terminate();
    if (!backgroundProcess_->waitForFinished(5000)) {
        qWarning() << "[ServiceManager] Background process did not terminate, killing...";
        backgroundProcess_->kill();
        backgroundProcess_->waitForFinished(2000);
    }

    backgroundPid_ = 0;
    return true;
}

bool ClientServiceManager::hideConsole() {
    qDebug() << "[ServiceManager] Hiding console window";
    hideConsoleWindow();
    currentMode_ = MODE_HIDDEN;
    return true;
}

bool ClientServiceManager::showConsole() {
    qDebug() << "[ServiceManager] Showing console window";
    showConsoleWindow();
    currentMode_ = MODE_CONSOLE;
    return true;
}

// ================================================================
// 자동 시작 관리
// ================================================================

bool ClientServiceManager::enableAutoStart() {
    qDebug() << "[ServiceManager] Enabling auto start";
    return addToRegistry();
}

bool ClientServiceManager::disableAutoStart() {
    qDebug() << "[ServiceManager] Disabling auto start";
    return removeFromRegistry();
}

bool ClientServiceManager::isAutoStartEnabled() {
    return isInRegistry();
}

// ================================================================
// 프로세스 관리
// ================================================================

bool ClientServiceManager::createBackgroundProcess(const QStringList& arguments) {
    if (backgroundProcess_) {
        delete backgroundProcess_;
    }

    backgroundProcess_ = new QProcess(this);
    connect(backgroundProcess_, &QProcess::finished,
            this, &ClientServiceManager::onBackgroundProcessFinished);

    QString executable = QCoreApplication::applicationFilePath();

    qDebug() << QString("[ServiceManager] Starting process: %1 %2")
                    .arg(executable).arg(arguments.join(" "));

    backgroundProcess_->start(executable, arguments);

    if (!backgroundProcess_->waitForStarted(5000)) {
        qWarning() << "[ServiceManager] Failed to start background process";
        delete backgroundProcess_;
        backgroundProcess_ = nullptr;
        return false;
    }

    backgroundPid_ = backgroundProcess_->processId();
    emit backgroundProcessStarted(backgroundPid_);

    qInfo() << QString("[ServiceManager] Background process started with PID: %1").arg(backgroundPid_);
    return true;
}

bool ClientServiceManager::isBackgroundProcessRunning() {
    if (!backgroundProcess_) {
        return false;
    }

    return backgroundProcess_->state() == QProcess::Running;
}

// ================================================================
// 내부 메서드들
// ================================================================

void ClientServiceManager::setupProcessMonitoring() {
    processMonitor_ = new QTimer(this);
    connect(processMonitor_, &QTimer::timeout, this, &ClientServiceManager::checkBackgroundProcess);
    processMonitor_->start(5000); // 5초마다 확인
}

void ClientServiceManager::cleanupProcessMonitoring() {
    if (processMonitor_) {
        processMonitor_->stop();
        delete processMonitor_;
        processMonitor_ = nullptr;
    }
}

void ClientServiceManager::checkBackgroundProcess() {
    if (backgroundProcess_ && backgroundProcess_->state() != QProcess::Running && backgroundPid_ != 0) {
        qWarning() << "[ServiceManager] Background process terminated unexpectedly";
        backgroundPid_ = 0;
        emit backgroundProcessFinished(-1);
    }
}

void ClientServiceManager::onBackgroundProcessFinished(int exitCode) {
    qInfo() << QString("[ServiceManager] Background process finished with exit code: %1").arg(exitCode);
    backgroundPid_ = 0;
    emit backgroundProcessFinished(exitCode);
}

// ================================================================
// 레지스트리 관련 (Windows 전용)
// ================================================================

bool ClientServiceManager::addToRegistry() {
#ifdef _WIN32
    try {
        QSettings registry("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                           QSettings::NativeFormat);

        // 실행 파일 이름 자동 감지
        QFileInfo fileInfo(QCoreApplication::applicationFilePath());
        QString appName = fileInfo.baseName(); // "unknown_client.exe" → "unknown_client"

        // ✅ 실행 파일 경로와 작업 디렉토리 분리
        QString exePath = fileInfo.absoluteFilePath();  // 전체 경로
        QString exeDir = fileInfo.absolutePath();       // 디렉토리 경로

        // ✅ cmd /c를 사용하여 작업 디렉토리 변경 후 실행
        // cd /d: 드라이브까지 포함하여 디렉토리 변경
        QString appPath = QString("cmd /c \"cd /d \"%1\" && \"%2\" --background --auto-start --no-console\"")
                              .arg(exeDir)
                              .arg(exePath);

        qDebug() << QString("[ServiceManager] Registering auto-start: Name=%1, Path=%2").arg(appName).arg(appPath);

        // 레지스트리에 값 쓰기
        registry.setValue(appName, appPath);
        registry.sync();

        // ✅ 기본 상태 확인
        if (registry.status() != QSettings::NoError) {
            qWarning() << "[ServiceManager] Failed to write to registry (QSettings error)";
            return false;
        }

        // ✅ 추가 검증: 실제로 레지스트리에 쓰여졌는지 재확인
        QSettings verifyRegistry("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                                 QSettings::NativeFormat);

        if (!verifyRegistry.contains(appName)) {
            qWarning() << "[ServiceManager] Verification failed: Registry key not found after write";
            return false;
        }

        QString writtenValue = verifyRegistry.value(appName).toString();
        if (writtenValue != appPath) {
            qWarning() << QString("[ServiceManager] Verification failed: Written value mismatch");
            qWarning() << QString("[ServiceManager] Expected: %1").arg(appPath);
            qWarning() << QString("[ServiceManager] Actual: %1").arg(writtenValue);
            return false;
        }

        // ✅ 모든 검증 통과
        qInfo() << QString("[ServiceManager] Auto start enabled in registry: %1").arg(appName);
        qInfo() << QString("[ServiceManager] Verified path: %1").arg(writtenValue);
        return true;

    } catch (const std::exception& e) {
        qWarning() << QString("[ServiceManager] Registry error: %1").arg(e.what());
        return false;
    }
#else
    qWarning() << "[ServiceManager] Auto start not supported on this platform";
    return false;
#endif
}

bool ClientServiceManager::removeFromRegistry() {
#ifdef _WIN32
    try {
        QSettings registry("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                           QSettings::NativeFormat);

        // 🔥 실행 파일 이름 자동 감지
        QFileInfo fileInfo(QCoreApplication::applicationFilePath());
        QString appName = fileInfo.baseName(); // "unknown_client"

        qDebug() << QString("[ServiceManager] Removing auto-start: %1").arg(appName);

        registry.remove(appName);
        registry.sync();

        qInfo() << QString("[ServiceManager] Auto start disabled in registry: %1").arg(appName);
        return true;
    } catch (const std::exception& e) {
        qWarning() << QString("[ServiceManager] Registry error: %1").arg(e.what());
        return false;
    }
#else
    return false;
#endif
}

bool ClientServiceManager::isInRegistry() {
#ifdef _WIN32
    try {
        QSettings registry("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                           QSettings::NativeFormat);

        // 🔥 실행 파일 이름 자동 감지
        QFileInfo fileInfo(QCoreApplication::applicationFilePath());
        QString appName = fileInfo.baseName(); // "unknown_client"

        bool exists = registry.contains(appName);

        qDebug() << QString("[ServiceManager] Checking auto-start: %1 = %2").arg(appName).arg(exists ? "YES" : "NO");

        return exists;
    } catch (const std::exception& e) {
        qWarning() << QString("[ServiceManager] Registry error: %1").arg(e.what());
        return false;
    }
#else
    return false;
#endif
}

// ================================================================
// 콘솔 창 관리 (Windows 전용)
// ================================================================

void ClientServiceManager::hideConsoleWindow() {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, SW_HIDE);
        consoleHidden_ = true;
        qDebug() << "[ServiceManager] Console window hidden";
    } else {
        qWarning() << "[ServiceManager] No console window to hide";
    }
#endif
}

void ClientServiceManager::showConsoleWindow() {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, SW_SHOW);
        consoleHidden_ = false;
        qDebug() << "[ServiceManager] Console window shown";
    } else {
        qWarning() << "[ServiceManager] No console window to show";
    }
#endif
}
