// ClientServiceManager.h - 간소화된 백그라운드 서비스 관리
#ifndef CLIENTSERVICEMANAGER_H
#define CLIENTSERVICEMANAGER_H

#include "pch.h"

// =================================================================
// ClientServiceManager - 간소화된 백그라운드 실행 관리
// 핵심 기능: 관리자 권한, 백그라운드 실행, 자동 시작
// =================================================================

class ClientServiceManager : public QObject {
    Q_OBJECT

public:
    // 실행 모드
    enum RunMode {
        MODE_CONSOLE = 0,           // 콘솔 모드
        MODE_BACKGROUND = 1,        // 백그라운드 모드
        MODE_HIDDEN = 2             // 숨김 모드
    };

    // 자동 시작 유형
    enum AutoStartType {
        AUTOSTART_NONE = 0,         // 자동 시작 안함
        AUTOSTART_REGISTRY = 1      // 레지스트리 등록 (간단)
    };

public:
    explicit ClientServiceManager(QObject* parent = nullptr);
    virtual ~ClientServiceManager();

    // 실행 모드 관리
    RunMode getCurrentRunMode() const { return currentMode_; }
    bool setRunMode(RunMode mode);
    bool isRunningInBackground() const;

    // 백그라운드 실행
    bool startBackground();
    bool stopBackground();
    bool hideConsole();
    bool showConsole();

    // 자동 시작 관리
    bool enableAutoStart();
    bool disableAutoStart();
    bool isAutoStartEnabled();

    // 프로세스 관리
    bool createBackgroundProcess(const QStringList& arguments = QStringList());
    bool isBackgroundProcessRunning();
    qint64 getBackgroundProcessId() const { return backgroundPid_; }

signals:
    void runModeChanged(RunMode newMode);
    void backgroundProcessStarted(qint64 pid);
    void backgroundProcessFinished(int exitCode);

private slots:
    void onBackgroundProcessFinished(int exitCode);
    void checkBackgroundProcess();

private:
    // 멤버 변수
    RunMode currentMode_;
    qint64 backgroundPid_;
    QProcess* backgroundProcess_;
    QTimer* processMonitor_;

    // Windows 콘솔 관리
    bool consoleHidden_;

    // 내부 메서드들
    void setupProcessMonitoring();
    void cleanupProcessMonitoring();

    // 레지스트리 관련 (Windows 전용)
    bool addToRegistry();
    bool removeFromRegistry();
    bool isInRegistry();

    // 콘솔 창 관리 (Windows 전용)
    void hideConsoleWindow();
    void showConsoleWindow();
};

#endif // CLIENTSERVICEMANAGER_H
