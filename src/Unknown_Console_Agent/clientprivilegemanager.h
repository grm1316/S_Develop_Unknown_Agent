// ClientPrivilegeManager.h - 클라이언트 권한 관리
#ifndef CLIENTPRIVILEGEMANAGER_H
#define CLIENTPRIVILEGEMANAGER_H

#include "pch.h"

// =================================================================
// ClientPrivilegeManager - 관리자 권한 확인 및 요청
// UAC 승격, 권한 확인, 자동 재시작 기능
// =================================================================

class ClientPrivilegeManager : public QObject {
    Q_OBJECT

public:
    // 권한 상태 열거형
    enum PrivilegeLevel {
        PRIVILEGE_UNKNOWN = 0,      // 확인되지 않음
        PRIVILEGE_USER = 1,         // 일반 사용자
        PRIVILEGE_ELEVATED = 2,     // 관리자 권한
        PRIVILEGE_SYSTEM = 3        // 시스템 권한 (서비스)
    };

    // 권한 요청 결과
    enum ElevationResult {
        ELEVATION_SUCCESS = 0,      // 성공
        ELEVATION_CANCELLED = 1,    // 사용자가 취소
        ELEVATION_FAILED = 2,       // 실패
        ELEVATION_NOT_NEEDED = 3    // 이미 관리자
    };

public:
    explicit ClientPrivilegeManager(QObject* parent = nullptr);
    virtual ~ClientPrivilegeManager();

    // 권한 확인
    static PrivilegeLevel getCurrentPrivilegeLevel();
    static bool isElevated();
    static bool isRunningAsService();
    static bool isUACEnabled();

    // 권한 요청
    static ElevationResult requestElevation(const QString& reason = "");
    static bool restartAsAdministrator(const QStringList& arguments = QStringList());

    // 권한 관련 유틸리티
    static QString getPrivilegeLevelString(PrivilegeLevel level);
    static QString getCurrentUserName();
    static QString getCurrentUserSID();
    static bool hasPrivilege(const QString& privilegeName);

    // 서비스 관련
    static bool installAsService(const QString& serviceName = "ForensicAgent");
    static bool uninstallService(const QString& serviceName = "ForensicAgent");
    static bool startService(const QString& serviceName = "ForensicAgent");
    static bool stopService(const QString& serviceName = "ForensicAgent");
    static bool isServiceInstalled(const QString& serviceName = "ForensicAgent");

signals:
    void privilegeChanged(PrivilegeLevel newLevel);
    void elevationRequested();
    void elevationCompleted(ElevationResult result);

private:
    // Windows API 헬퍼 함수들
    static bool checkTokenElevation();
    static bool enablePrivilege(const QString& privilegeName);
    static QString getLastErrorString();
    static bool createElevatedProcess(const QString& executable, const QStringList& arguments);

    // 서비스 관련 내부 함수들
    static bool registerServiceBinary(const QString& serviceName, const QString& binaryPath);
    static bool setServiceDescription(const QString& serviceName, const QString& description);
    static bool configureServiceRecovery(const QString& serviceName);

private:
    PrivilegeLevel currentLevel_;
    bool monitoringEnabled_;
};

#endif // CLIENTPRIVILEGEMANAGER_H
