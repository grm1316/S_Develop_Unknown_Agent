// ClientPrivilegeManager.cpp - 클라이언트 권한 관리 구현
#include "ClientPrivilegeManager.h"

#ifdef _WIN32
#include <sddl.h>
#include <userenv.h>
#include <lm.h>
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "netapi32.lib")
#endif

ClientPrivilegeManager::ClientPrivilegeManager(QObject* parent)
    : QObject(parent)
    , currentLevel_(PRIVILEGE_UNKNOWN)
    , monitoringEnabled_(false)
{
    currentLevel_ = getCurrentPrivilegeLevel();
    qDebug() << QString("[PrivilegeManager] Current privilege level: %1")
                    .arg(getPrivilegeLevelString(currentLevel_));
}

ClientPrivilegeManager::~ClientPrivilegeManager() {
    // 정리 작업
}

// ================================================================
// 권한 확인 메서드들
// ================================================================

ClientPrivilegeManager::PrivilegeLevel ClientPrivilegeManager::getCurrentPrivilegeLevel() {
#ifdef _WIN32
    // 서비스로 실행 중인지 확인
    if (isRunningAsService()) {
        return PRIVILEGE_SYSTEM;
    }

    // 관리자 권한 확인
    if (isElevated()) {
        return PRIVILEGE_ELEVATED;
    }

    return PRIVILEGE_USER;
#else
    // Linux/Unix에서는 UID 확인
    if (geteuid() == 0) {
        return PRIVILEGE_ELEVATED;
    }
    return PRIVILEGE_USER;
#endif
}

bool ClientPrivilegeManager::isElevated() {
#ifdef _WIN32
    return checkTokenElevation();
#else
    return geteuid() == 0;
#endif
}

bool ClientPrivilegeManager::checkTokenElevation() {
#ifdef _WIN32
    HANDLE hToken = NULL;
    TOKEN_ELEVATION elevation;
    DWORD dwSize;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        qWarning() << "[PrivilegeManager] Failed to open process token";
        return false;
    }

    if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
        qWarning() << "[PrivilegeManager] Failed to get token elevation info";
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    return elevation.TokenIsElevated != 0;
#else
    return false;
#endif
}

bool ClientPrivilegeManager::isRunningAsService() {
#ifdef _WIN32
    // 세션 ID 확인 (서비스는 세션 0에서 실행)
    DWORD sessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        if (sessionId == 0) {
            // 세션 0이지만 콘솔이 있는지 확인
            HWND console = GetConsoleWindow();
            return (console == NULL);
        }
    }
    return false;
#else
    return false;
#endif
}

bool ClientPrivilegeManager::isUACEnabled() {
#ifdef _WIN32
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        RegQueryValueExA(hKey, "EnableLUA", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
        return (value != 0);
    }
    return true; // 기본적으로 활성화되어 있다고 가정
#else
    return false;
#endif
}

// ================================================================
// 권한 요청 메서드들
// ================================================================

ClientPrivilegeManager::ElevationResult ClientPrivilegeManager::requestElevation(const QString& reason) {
    if (isElevated()) {
        qInfo() << "[PrivilegeManager] Already running with elevated privileges";
        return ELEVATION_NOT_NEEDED;
    }

    qInfo() << QString("[PrivilegeManager] Requesting elevation: %1").arg(reason.isEmpty() ? "Administrative access required" : reason);

    // 현재 실행 파일 경로와 인수 가져오기
    QString currentExe = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    args.removeFirst(); // 실행 파일 경로 제거

    // 🔍 중요: --elevated 인수가 이미 있는지 확인하고 없으면 추가
    if (!args.contains("--elevated")) {
        args.append("--elevated"); // 승격 표시 인수 추가
    }

    qDebug() << QString("[PrivilegeManager] Restarting with args: %1").arg(args.join(" "));

    if (restartAsAdministrator(args)) {
        // 승격된 프로세스가 시작되면 현재 프로세스 종료
        qInfo() << "[PrivilegeManager] Elevated process started, exiting current process";
        QCoreApplication::quit();
        return ELEVATION_SUCCESS;
    } else {
        qWarning() << "[PrivilegeManager] Failed to start elevated process";
        return ELEVATION_FAILED;
    }
}

bool ClientPrivilegeManager::restartAsAdministrator(const QStringList& arguments) {
#ifdef _WIN32
    QString executable = QCoreApplication::applicationFilePath();
    QString params = arguments.join(" ");

    qDebug() << QString("[PrivilegeManager] Restarting as admin: %1 %2").arg(executable).arg(params);

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow = SW_NORMAL;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            CloseHandle(sei.hProcess);
        }
        return true;
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            qWarning() << "[PrivilegeManager] User cancelled UAC prompt";
        } else {
            qWarning() << QString("[PrivilegeManager] ShellExecuteEx failed: %1").arg(error);
        }
        return false;
    }
#else
    Q_UNUSED(arguments)
    return false;
#endif
}

// ================================================================
// 유틸리티 메서드들
// ================================================================

QString ClientPrivilegeManager::getPrivilegeLevelString(PrivilegeLevel level) {
    switch (level) {
    case PRIVILEGE_UNKNOWN:  return "Unknown";
    case PRIVILEGE_USER:     return "User";
    case PRIVILEGE_ELEVATED: return "Administrator";
    case PRIVILEGE_SYSTEM:   return "System Service";
    default:                 return "Invalid";
    }
}

QString ClientPrivilegeManager::getCurrentUserName() {
#ifdef _WIN32
    wchar_t username[UNLEN + 1];
    DWORD size = UNLEN + 1;
    if (GetUserNameW(username, &size)) {
        return QString::fromWCharArray(username);
    }
    return "Unknown";
#else
    return QString(getenv("USER"));
#endif
}

QString ClientPrivilegeManager::getCurrentUserSID() {
#ifdef _WIN32
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return QString();
    }

    DWORD tokenSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &tokenSize);

    TOKEN_USER* tokenUser = (TOKEN_USER*)malloc(tokenSize);
    if (!GetTokenInformation(hToken, TokenUser, tokenUser, tokenSize, &tokenSize)) {
        free(tokenUser);
        CloseHandle(hToken);
        return QString();
    }

    LPWSTR sidString;
    if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidString)) {
        QString result = QString::fromWCharArray(sidString);
        LocalFree(sidString);
        free(tokenUser);
        CloseHandle(hToken);
        return result;
    }

    free(tokenUser);
    CloseHandle(hToken);
    return QString();
#else
    return QString::number(getuid());
#endif
}

bool ClientPrivilegeManager::hasPrivilege(const QString& privilegeName) {
#ifdef _WIN32
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    LUID luid;
    if (!LookupPrivilegeValueA(NULL, privilegeName.toLocal8Bit().constData(), &luid)) {
        CloseHandle(hToken);
        return false;
    }

    PRIVILEGE_SET ps;
    ps.PrivilegeCount = 1;
    ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
    ps.Privilege[0].Luid = luid;
    ps.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL result;
    if (!PrivilegeCheck(hToken, &ps, &result)) {
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    return result != FALSE;
#else
    Q_UNUSED(privilegeName)
    return isElevated();
#endif
}

// ================================================================
// 서비스 관련 메서드들 (향후 구현)
// ================================================================

bool ClientPrivilegeManager::installAsService(const QString& serviceName) {
    // TODO: 서비스 설치 구현
    Q_UNUSED(serviceName)
    qDebug() << "[PrivilegeManager] Service installation not implemented yet";
    return false;
}

bool ClientPrivilegeManager::uninstallService(const QString& serviceName) {
    // TODO: 서비스 제거 구현
    Q_UNUSED(serviceName)
    qDebug() << "[PrivilegeManager] Service uninstallation not implemented yet";
    return false;
}

bool ClientPrivilegeManager::startService(const QString& serviceName) {
    Q_UNUSED(serviceName)
    return false;
}

bool ClientPrivilegeManager::stopService(const QString& serviceName) {
    Q_UNUSED(serviceName)
    return false;
}

bool ClientPrivilegeManager::isServiceInstalled(const QString& serviceName) {
    Q_UNUSED(serviceName)
    return false;
}

QString ClientPrivilegeManager::getLastErrorString() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return QString();

    LPWSTR messageBuffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&messageBuffer, 0, NULL);

    QString message = QString::fromWCharArray(messageBuffer);
    LocalFree(messageBuffer);
    return message.trimmed();
#else
    return QString();
#endif
}
