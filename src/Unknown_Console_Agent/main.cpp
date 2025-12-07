#include "pch.h"
#include "ClientNetworkManager.h"
#include "ClientUtils.h"
#include "messengercollector.h"
#include "externalstorage.h"
#include "prefetch.h"
#include "deleteddatacollector.h"
#include "lnkcollector.h"
#include "SimpleBrowserCollector.h"
#include "ClientPrivilegeManager.h"
#include "ClientServiceManager.h"
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QMetaObject>
#include <iostream>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

// =================================================================
// 전역 변수
// =================================================================

static ClientNetworkManager* g_networkManager = nullptr;
static ClientServiceManager* g_serviceManager = nullptr;
static QString g_outputDir;
static bool g_shutdownRequested = false;

#ifdef _WIN32
static HANDLE g_singleInstanceMutex = nullptr;
#endif

// =================================================================
// 프로세스 중복 실행 방지
// =================================================================

bool ensureSingleInstance() {
#ifdef _WIN32
    g_singleInstanceMutex = CreateMutexW(NULL, TRUE, L"ForensicAgent_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        qWarning() << "[Main] Another instance is already running";
        if (g_singleInstanceMutex) {
            CloseHandle(g_singleInstanceMutex);
        }
        return false;
    }
    qInfo() << "[Main] Single instance mutex created";
    return true;
#else
    return true;
#endif
}

void releaseSingleInstance() {
#ifdef _WIN32
    if (g_singleInstanceMutex) {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        qInfo() << "[Main] Single instance mutex released";
    }
#endif
}

// =================================================================
// 콘솔 설정
// =================================================================

void setupConsole() {
#ifdef _WIN32
    // Windows 콘솔 UTF-8 설정 (간단 버전)
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

// =================================================================
// Qt 메시지 핸들러 (한글 출력 지원)
// =================================================================

#ifdef _WIN32
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        return;
    }

    // 메시지 타입에 따른 색상 설정
    WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // 기본 흰색

    QString prefix;
    switch (type) {
    case QtDebugMsg:
        color = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        prefix = "[DEBUG] ";
        break;
    case QtInfoMsg:
        color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        prefix = "";
        break;
    case QtWarningMsg:
        color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        prefix = "[WARNING] ";
        break;
    case QtCriticalMsg:
        color = FOREGROUND_RED | FOREGROUND_INTENSITY;
        prefix = "[CRITICAL] ";
        break;
    case QtFatalMsg:
        color = FOREGROUND_RED | FOREGROUND_INTENSITY;
        prefix = "[FATAL] ";
        break;
    }

    // 색상 설정
    SetConsoleTextAttribute(hConsole, color);

    // 메시지 출력 (UTF-16으로 변환하여 WriteConsoleW 사용)
    QString output = prefix + msg + "\n";
    WriteConsoleW(hConsole, output.toStdWString().c_str(),
                  output.toStdWString().length(), nullptr, nullptr);

    // 기본 색상으로 복원
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    // Fatal 에러는 프로그램 종료
    if (type == QtFatalMsg) {
        abort();
    }
}
#endif

// =================================================================
// Task 처리 함수들
// =================================================================

bool executeUSBCollection(const QString& taskId) {
    ClientUtils::showProgress("USB 데이터 수집 중", 10);

    try {
        externalstorage usbCollector;
        if (usbCollector.collectUSBForensicsData()) {
            QJsonObject usbJson = usbCollector.toJsonObject();

            if (!ClientUtils::validateJsonData(usbJson)) {
                ClientUtils::showWarningMessage("JSON 검증", "USB 데이터에 유효하지 않은 값이 있음");
            }

            QJsonDocument doc(usbJson);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

            bool success = g_networkManager->sendForensicDataWithTaskId(
                taskId, "USB_DATA", "usb_data.json", jsonData);

            if (success) {
                ClientUtils::showSuccessMessage("USB 수집", "데이터 전송 완료");
                return true;
            } else {
                ClientUtils::showErrorMessage("USB 수집", "데이터 전송 실패");
                return false;
            }
        } else {
            ClientUtils::showInfoMessage("USB 수집", "USB 장치를 찾을 수 없음");
            return true;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("USB 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

bool executeBrowserCollection(const QString& taskId) {
    ClientUtils::showProgress("브라우저 데이터 수집 중", 25);

    try {
        SimpleBrowserCollector browserCollector;
        if (browserCollector.collectAllBrowserData()) {
            QJsonObject browserJson = browserCollector.toDetailedJsonObject();

            if (!ClientUtils::validateJsonData(browserJson)) {
                ClientUtils::showWarningMessage("JSON 검증", "브라우저 데이터에 유효하지 않은 값이 있음");
            }

            QJsonDocument doc(browserJson);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

            bool success = g_networkManager->sendForensicDataWithTaskId(
                taskId, "BROWSER_DATA", "browser_data.json", jsonData);

            if (success) {
                const auto& stats = browserCollector.getCollectionStats();
                ClientUtils::showSuccessMessage("브라우저 수집",
                                                QString("%1개 파일 전송 완료").arg(stats.successFiles));
                return true;
            } else {
                ClientUtils::showErrorMessage("브라우저 수집", "데이터 전송 실패");
                return false;
            }
        } else {
            ClientUtils::showInfoMessage("브라우저 수집", "브라우저 데이터를 찾을 수 없음");
            return true;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("브라우저 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

bool executePrefetchCollection(const QString& taskId) {
    ClientUtils::showProgress("프리패치 데이터 수집 중", 40);

    try {
        PrefetchCollector prefetchCollector;
        prefetchCollector.collectFromDirectory();
        QJsonObject prefetchJson = prefetchCollector.toJsonObject();

        if (!ClientUtils::validateJsonData(prefetchJson)) {
            ClientUtils::showWarningMessage("JSON 검증", "프리패치 데이터에 유효하지 않은 값이 있음");
        }

        QJsonDocument doc(prefetchJson);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

        bool success = g_networkManager->sendForensicDataWithTaskId(
            taskId, "PREFETCH_DATA", "prefetch_data.json", jsonData);

        if (success) {
            ClientUtils::showSuccessMessage("프리패치 수집",
                                            QString("%1개 파일 전송 완료").arg(prefetchCollector.getSuccessCount()));
            return true;
        } else {
            ClientUtils::showErrorMessage("프리패치 수집", "데이터 전송 실패");
            return false;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("프리패치 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

bool executeLNKCollection(const QString& taskId) {
    ClientUtils::showProgress("LNK 파일 수집 중", 55);

    try {
        LnkCollector lnkCollector;
        if (lnkCollector.collectLnkFiles()) {
            QJsonObject lnkJson = lnkCollector.toJsonObject();

            if (!ClientUtils::validateJsonData(lnkJson)) {
                ClientUtils::showWarningMessage("JSON 검증", "LNK 데이터에 유효하지 않은 값이 있음");
            }

            QJsonDocument doc(lnkJson);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

            bool success = g_networkManager->sendForensicDataWithTaskId(
                taskId, "LNK_DATA", "lnk_data.json", jsonData);

            if (success) {
                ClientUtils::showSuccessMessage("LNK 수집",
                                                QString("%1개 파일 전송 완료").arg(lnkCollector.getCollectedCount()));
                return true;
            } else {
                ClientUtils::showErrorMessage("LNK 수집", "데이터 전송 실패");
                return false;
            }
        } else {
            ClientUtils::showInfoMessage("LNK 수집", "LNK 파일을 찾을 수 없음");
            return true;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("LNK 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

bool executeDeletedFilesCollection(const QString& taskId) {
    ClientUtils::showProgress("삭제된 파일 수집 중", 70);

    try {
        DeletedDataCollector deletedCollector;
        if (deletedCollector.collectAllDeletedFiles()) {
            QJsonObject deletedJson = deletedCollector.toJsonObject();

            if (!ClientUtils::validateJsonData(deletedJson)) {
                ClientUtils::showWarningMessage("JSON 검증", "삭제 데이터에 유효하지 않은 값이 있음");
            }

            QJsonDocument doc(deletedJson);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

            bool success = g_networkManager->sendForensicDataWithTaskId(
                taskId, "DELETED_FILES", "deleted_files.json", jsonData);

            if (success) {
                ClientUtils::showSuccessMessage("삭제 파일 수집",
                                                QString("%1개 파일 전송 완료").arg(deletedCollector.getDeletedFileCount()));
                return true;
            } else {
                ClientUtils::showErrorMessage("삭제 파일 수집", "데이터 전송 실패");
                return false;
            }
        } else {
            ClientUtils::showInfoMessage("삭제 파일 수집", "삭제된 파일을 찾을 수 없음");
            return true;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("삭제 파일 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

bool executeMessengerCollection(const QString& taskId) {
    ClientUtils::showProgress("메신저 데이터 수집 중", 85);

    try {
        MessengerCollector messengerCollector;
        if (messengerCollector.collectMessengerForensicsData()) {
            QJsonObject messengerJson = messengerCollector.toJsonObject();

            if (!ClientUtils::validateJsonData(messengerJson)) {
                ClientUtils::showWarningMessage("JSON 검증", "메신저 데이터에 유효하지 않은 값이 있음");
            }

            QJsonDocument doc(messengerJson);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

            bool success = g_networkManager->sendForensicDataWithTaskId(
                taskId, "MESSENGER_DATA", "messenger_data.json", jsonData);

            if (success) {
                ClientUtils::showSuccessMessage("메신저 수집",
                                                QString("%1개 파일 전송 완료").arg(messengerCollector.getFileList().size()));
                return true;
            } else {
                ClientUtils::showErrorMessage("메신저 수집", "데이터 전송 실패");
                return false;
            }
        } else {
            ClientUtils::showInfoMessage("메신저 수집", "메신저 데이터를 찾을 수 없음");
            return true;
        }
    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("메신저 수집", QString("예외 발생: %1").arg(e.what()));
        return false;
    }
}

// =================================================================
// Task 처리 핸들러
// =================================================================

void handleTaskExecution(const ClientNetworkManager::TaskRequest& task) {
    ClientUtils::showInfoMessage("Task 실행", QString("ID: %1, Type: %2").arg(task.taskId, task.taskType));

    bool success = false;

    if (task.taskType == "USB_DATA" || task.taskType == "EXTERNAL_STORAGE") {
        success = executeUSBCollection(task.taskId);
    }
    else if (task.taskType == "BROWSER_DATA") {
        success = executeBrowserCollection(task.taskId);
    }
    else if (task.taskType == "PREFETCH_DATA") {
        success = executePrefetchCollection(task.taskId);
    }
    else if (task.taskType == "LNK_DATA") {
        success = executeLNKCollection(task.taskId);
    }
    else if (task.taskType == "DELETED_FILES") {
        success = executeDeletedFilesCollection(task.taskId);
    }
    else if (task.taskType == "MESSENGER_DATA") {
        success = executeMessengerCollection(task.taskId);
    }
    else if (task.taskType == "ALL_DATA") {
        ClientUtils::showProgress("전체 데이터 수집 시작", 0);
        int successCount = 0;

        if (executeUSBCollection(task.taskId)) successCount++;
        if (executeBrowserCollection(task.taskId)) successCount++;
        if (executePrefetchCollection(task.taskId)) successCount++;
        if (executeLNKCollection(task.taskId)) successCount++;
        if (executeDeletedFilesCollection(task.taskId)) successCount++;
        if (executeMessengerCollection(task.taskId)) successCount++;

        success = (successCount > 0);
        ClientUtils::showProgress("전체 데이터 수집 완료", 100);
        ClientUtils::showInfoMessage("전체 수집", QString("%1/6개 모듈 성공").arg(successCount));
    }
    else {
        ClientUtils::showErrorMessage("Task 오류", QString("지원하지 않는 Task 타입: %1").arg(task.taskType));
        success = false;
    }
}

// =================================================================
// Owner_ID 입력 처리
// =================================================================

QString promptOwnerIdFromUser() {
    ClientUtils::showWarningMessage("Owner_ID Required", "Please enter Owner_ID for new PC registration.");
    ClientUtils::showInfoMessage("Input Guide", "Please enter Owner_ID in console and press Enter.");

    std::cout << "\nPlease enter Owner_ID: ";
    std::cout.flush();

    std::string input;
    std::getline(std::cin, input);

    QString ownerId = QString::fromStdString(input).trimmed();

    if (ownerId.isEmpty()) {
        ClientUtils::showErrorMessage("Input Error", "Owner_ID was not entered.");
        return QString();
    }

    ClientUtils::showInfoMessage("Input Complete", QString("Owner_ID: %1").arg(ownerId));
    return ownerId;
}

// =================================================================
// 시그널 핸들러 설정
// =================================================================

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal) {
    switch (signal) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_BREAK_EVENT:
    {
        qInfo() << "[Main] Console signal received, initiating shutdown...";
        ClientUtils::showWarningMessage("종료 신호", "프로그램 종료 중...");
        g_shutdownRequested = true;

        // 네트워크 연결 종료
        if (g_networkManager) {
            qInfo() << "[Main] Disconnecting from server...";
            g_networkManager->disconnectFromServer();
        }

        // 핵심 수정: 메인 스레드에서 QCoreApplication::quit() 호출
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);

        qInfo() << "[Main] Shutdown initiated";
        return TRUE;
    }
    default:
        return FALSE;
    }
}
#endif

// =================================================================
// 메인 함수
// =================================================================

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // =================================================================
    // Qt 메시지 핸들러 설치 (한글 출력 지원)
    // =================================================================
#ifdef _WIN32
    qInstallMessageHandler(customMessageHandler);
#endif

    // =================================================================
    // 1단계: 명령줄 인수 파싱
    // =================================================================

    bool flagElevated = false;
    bool flagBackground = false;
    bool flagAutoStart = false;
    bool flagNoConsole = false;
    bool flagUninstallAutoStart = false;

    qInfo() << "[Main] Parsing command line arguments...";

    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        qDebug() << QString("[Main] Argument %1: %2").arg(i).arg(arg);

        if (arg == "--elevated") {
            flagElevated = true;
            qInfo() << "[Main] Flag detected: --elevated (already elevated)";
        }
        else if (arg == "--background") {
            flagBackground = true;
            qInfo() << "[Main] Flag detected: --background (background mode)";
        }
        else if (arg == "--auto-start") {
            flagAutoStart = true;
            qInfo() << "[Main] Flag detected: --auto-start (auto-start mode)";
        }
        else if (arg == "--no-console") {
            flagNoConsole = true;
            qInfo() << "[Main] Flag detected: --no-console (hide console)";
        }
        else if (arg == "--uninstall-autostart") {
            flagUninstallAutoStart = true;
            qInfo() << "[Main] Flag detected: --uninstall-autostart (remove auto-start)";
        }
    }

    // =================================================================
    // 2단계: 프로세스 중복 실행 방지
    // =================================================================

    if (!ensureSingleInstance()) {
        ClientUtils::showWarningMessage("중복 실행", "프로그램이 이미 실행 중입니다.");
        return 0;
    }

    // =================================================================
    // 2-1단계: 자동 시작 제거 요청 처리 (단독 실행)
    // =================================================================

    if (flagUninstallAutoStart) {
        qInfo() << "[Main] Uninstalling auto-start...";

        g_serviceManager = new ClientServiceManager(nullptr);

        ClientUtils::showInfoMessage("자동 시작 제거", "레지스트리에서 제거 중...");

        if (g_serviceManager->disableAutoStart()) {
            qInfo() << "[Main] Auto-start uninstalled successfully";
            ClientUtils::showSuccessMessage("자동 시작 제거 완료",
                                            "프로그램이 더 이상 자동으로 시작되지 않습니다.");

            delete g_serviceManager;
            releaseSingleInstance();
            return 0;
        } else {
            qWarning() << "[Main] Failed to uninstall auto-start";
            ClientUtils::showErrorMessage("자동 시작 제거 실패",
                                          "레지스트리 제거에 실패했습니다.");

            delete g_serviceManager;
            releaseSingleInstance();
            return 1;
        }
    }

    // =================================================================
    // 3단계: 콘솔 설정 - ✅ 항상 실행
    // =================================================================

#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif

    // ✅ 수정: 항상 콘솔 초기화 및 출력
    setupConsole();
    ClientUtils::clearConsole();
    ClientUtils::showInfoMessage("포렌식 클라이언트", "시작됨");
    ClientUtils::showInfoMessage("클라이언트 정보", QString("이름: %1").arg(ClientUtils::getClientDisplayName()));

    // 출력 디렉토리 설정
    g_outputDir = QDir::currentPath();

    // =================================================================
    // 4단계: 관리자 권한 확인 및 승격
    // =================================================================

    ClientPrivilegeManager::PrivilegeLevel privilege = ClientPrivilegeManager::getCurrentPrivilegeLevel();

    // ✅ 수정: 항상 권한 상태 출력
    ClientUtils::showInfoMessage("권한 상태", ClientPrivilegeManager::getPrivilegeLevelString(privilege));

    // 핵심 수정: 백그라운드 모드에서는 UAC 승격 시도하지 않음
    if (flagBackground || flagAutoStart) {
        // 백그라운드 모드 - UAC 승격 시도하지 않음
        if (privilege != ClientPrivilegeManager::PRIVILEGE_ELEVATED) {
            qWarning() << "[Main] Running in background without administrator privileges";
            qWarning() << "[Main] Some features may be limited";

            // ✅ 수정: 항상 권한 제한 메시지 출력
            ClientUtils::showWarningMessage("권한 제한",
                                            "백그라운드 모드에서 관리자 권한 없이 실행 중입니다.\n일부 기능이 제한될 수 있습니다.");
        } else {
            qInfo() << "[Main] Running in background with administrator privileges";
        }

    } else {
        // 일반 콘솔 모드 - UAC 승격 시도
        if (!flagElevated && privilege != ClientPrivilegeManager::PRIVILEGE_ELEVATED) {
            qInfo() << "[Main] Administrator privileges required, requesting elevation...";

            // ✅ 수정: 항상 UAC 승격 요청 메시지 출력
            ClientUtils::showWarningMessage("권한 필요", "관리자 권한이 필요합니다. UAC 승격을 요청합니다.");

            ClientPrivilegeManager::ElevationResult result = ClientPrivilegeManager::requestElevation("Forensic data collection");

            if (result == ClientPrivilegeManager::ELEVATION_SUCCESS) {
                qInfo() << "[Main] Elevation successful, current process will exit";
                releaseSingleInstance();
                return 0;
            }
            else if (result == ClientPrivilegeManager::ELEVATION_CANCELLED) {
                ClientUtils::showWarningMessage("권한 거부", "사용자가 권한 승격을 취소했습니다. 일부 기능이 제한됩니다.");
                qWarning() << "[Main] User cancelled elevation, continuing with limited privileges";
            }
            else if (result == ClientPrivilegeManager::ELEVATION_FAILED) {
                ClientUtils::showErrorMessage("권한 오류", "권한 승격에 실패했습니다. 일부 기능이 제한됩니다.");
                qWarning() << "[Main] Elevation failed, continuing with limited privileges";
            }
        } else if (flagElevated) {
            qInfo() << "[Main] Running with elevated privileges (--elevated flag detected)";
        }
    }

    // =================================================================
    // 5단계: 백그라운드 모드 처리 및 자동 시작 관리
    // =================================================================

    g_serviceManager = new ClientServiceManager(&app);

    // 시나리오: 처음은 콘솔 모드, 이후는 백그라운드 자동 실행
    if (flagBackground && flagAutoStart) {
        // 부팅 시 자동 실행 (백그라운드 모드)
        qInfo() << "[Main] Running in AUTO-START BACKGROUND mode";
        qInfo() << "[Main] Started automatically at boot";

        // 콘솔 숨김
        if (flagNoConsole) {
            qInfo() << "[Main] Hiding console window";
            g_serviceManager->hideConsole();
        }

        qInfo() << "[Main] Will NOT create additional background processes";

    } else if (flagBackground) {
        // 백그라운드 모드 (수동 실행)
        qInfo() << "[Main] Running in BACKGROUND mode (manual start)";

        if (flagNoConsole) {
            qInfo() << "[Main] Hiding console window";
            g_serviceManager->hideConsole();
        }

    } else {
        // 일반 콘솔 모드 (처음 실행)
        qInfo() << "[Main] Running in CONSOLE mode (first run or manual start)";

        // 핵심: 자동 시작이 등록되어 있지 않으면 자동 등록
        if (!g_serviceManager->isAutoStartEnabled()) {
            qInfo() << "[Main] Auto-start not configured, installing...";

            // ✅ 수정: 항상 초기 설정 메시지 출력
            ClientUtils::showInfoMessage("초기 설정", "자동 시작 기능을 설정합니다...");

            if (g_serviceManager->enableAutoStart()) {
                qInfo() << "[Main] Auto-start installed successfully";

                // ✅ 수정: 항상 성공 메시지 출력
                ClientUtils::showSuccessMessage("자동 시작 설정 완료",
                                                "다음 부팅부터 프로그램이 백그라운드에서 자동으로 실행됩니다.");
                ClientUtils::showInfoMessage("알림",
                                             "현재는 콘솔 모드로 정상 작동을 확인할 수 있습니다.");
            } else {
                qWarning() << "[Main] Failed to install auto-start (권한 부족일 수 있음)";

                // ✅ 수정: 항상 실패 메시지 출력
                ClientUtils::showWarningMessage("자동 시작 설정 실패",
                                                "자동 시작 등록에 실패했습니다. 관리자 권한이 필요할 수 있습니다.");
            }
        } else {
            qInfo() << "[Main] Auto-start already configured";

            // ✅ 수정: 항상 자동 시작 정보 출력
            ClientUtils::showInfoMessage("자동 시작",
                                         "자동 시작이 이미 설정되어 있습니다. 다음 부팅 시 백그라운드로 실행됩니다.");
        }
    }
    // =================================================================
    // 6단계: 네트워크 매니저 초기화 및 서버 연결
    // =================================================================

    try {
        g_networkManager = new ClientNetworkManager(&app);

        // 시그널 연결
        QObject::connect(g_networkManager, &ClientNetworkManager::connected, []() {
            ClientUtils::showSuccessMessage("연결", "서버에 연결됨");
        });

        QObject::connect(g_networkManager, &ClientNetworkManager::disconnected, []() {
            ClientUtils::showWarningMessage("연결", "서버 연결 끊김");
        });

        QObject::connect(g_networkManager, &ClientNetworkManager::errorOccurred, [](const QString& error) {
            ClientUtils::showErrorMessage("네트워크 오류", error);
        });

        QObject::connect(g_networkManager, &ClientNetworkManager::registrationCompleted, [](const QString& pcId) {
            ClientUtils::showSuccessMessage("등록 완료", QString("PC ID: %1").arg(pcId));
        });

        QObject::connect(g_networkManager, &ClientNetworkManager::ownerIdRequired, []() {
            QString ownerId = promptOwnerIdFromUser();
            if (!ownerId.isEmpty()) {
                bool submitted = g_networkManager->submitOwnerID(ownerId);
                if (submitted) {
                    ClientUtils::showInfoMessage("처리 중", "서버 응답 처리 중...");
                }
            }
        });

        QObject::connect(g_networkManager, &ClientNetworkManager::taskReceived, handleTaskExecution);

        // 서버 연결 시도
        ClientUtils::showProgress("서버 연결 중", 0);

        if (g_networkManager->connectToServer()) {
            // 연결 성공
            ClientUtils::showProgress("연결 성공", 50);
            ClientUtils::showInfoMessage("상태", "Task 대기 중...");

            // ✅ 수정: 항상 종료 방법 안내 출력
            ClientUtils::showInfoMessage("종료", "Ctrl+C로 종료 가능");

            qInfo() << "[Main] Server connected successfully, starting event loop...";

        } else {
            // 핵심 수정: 백그라운드 모드에서는 연결 실패해도 계속 실행
            if (flagBackground || flagAutoStart) {
                qWarning() << "[Main] Initial server connection failed, but continuing in background mode";
                qWarning() << "[Main] Will retry connection every 10 seconds...";

                // ✅ 수정: 항상 연결 실패 메시지 출력
                ClientUtils::showWarningMessage("연결 실패", "서버 연결 실패. 백그라운드에서 재시도 중...");

                // 재연결 타이머 설정 (10초마다)
                QTimer* reconnectTimer = new QTimer(&app);
                QObject::connect(reconnectTimer, &QTimer::timeout, [&]() {
                    if (!g_networkManager->isConnected()) {
                        qInfo() << "[Main] Attempting to reconnect to server...";

                        if (g_networkManager->connectToServer()) {
                            qInfo() << "[Main] Reconnection successful!";
                            ClientUtils::showSuccessMessage("재연결", "서버에 재연결 성공");
                        } else {
                            qDebug() << "[Main] Reconnection failed, will retry...";
                        }
                    }
                });
                reconnectTimer->start(10000); // 10초마다 재시도

                qInfo() << "[Main] Background mode active, starting event loop...";

            } else {
                // 일반 모드에서는 기존대로 종료
                ClientUtils::showErrorMessage("연결 실패", "서버에 연결할 수 없습니다.");

                delete g_networkManager;
                delete g_serviceManager;

                releaseSingleInstance();
                return 1;
            }
        }

        // 이벤트 루프 시작 (백그라운드/일반 모드 공통)
        qInfo() << "[Main] Starting event loop...";

        // ✅ Windows 콘솔 stdin 문제 해결: 강제 이벤트 처리 타이머
        // stdin이 Qt 이벤트 루프를 방해하는 문제 해결
        QTimer* eventProcessTimer = new QTimer(&app);
        QObject::connect(eventProcessTimer, &QTimer::timeout, []() {
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        });
        eventProcessTimer->start(100); // 100ms마다 이벤트 강제 처리
        qInfo() << "[Main] Event process timer started (100ms interval)";

        int result = app.exec();

        // 정리
        qInfo() << "[Main] Event loop finished, cleaning up...";

        // 타이머 정리
        if (eventProcessTimer) {
            eventProcessTimer->stop();
            delete eventProcessTimer;
            eventProcessTimer = nullptr;
        }

        delete g_networkManager;
        g_networkManager = nullptr;

        delete g_serviceManager;
        g_serviceManager = nullptr;

        releaseSingleInstance();

        ClientUtils::showInfoMessage("종료", "프로그램이 정상 종료됨");
        return result;

    } catch (const std::exception& e) {
        ClientUtils::showErrorMessage("예외 발생", e.what());

        if (g_networkManager) {
            delete g_networkManager;
        }
        if (g_serviceManager) {
            delete g_serviceManager;
        }

        releaseSingleInstance();
        return 1;
    }
}
