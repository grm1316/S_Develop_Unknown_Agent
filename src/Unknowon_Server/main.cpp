// main.cpp - ForensicServer 전용 메인 함수

#include "pch.h"
#include "ForensicServer.h"
#include <QJsonObject>
#include <signal.h>
#include <QSettings>
#include <QFile>

// 전역 서버 인스턴스 (신호 핸들러용)
ForensicServer* g_server = nullptr;

// 신호 핸들러 (Ctrl+C, SIGTERM 등)
void signalHandler(int signal) {
    qWarning() << "\nSignal received:" << signal << ". Initiating graceful shutdown via event loop.";
    // Qt의 메인 이벤트 루프에 종료 이벤트를 보냅니다. 이 방식은 스레드에 안전합니다.
    // qApp은 QCoreApplication 인스턴스를 가리키는 전역 포인터입니다.
    QCoreApplication::quit();
}

// 간소화된 서버 상태 모니터링 함수
void monitorServerStatus(ForensicServer& server) {
    qWarning() << "========================================";
    qWarning() << "SERVER STATUS MONITORING";
    qWarning() << "========================================";

    auto stats = server.getStats();
    qWarning() << QString("Database Connected: %1").arg(server.isDatabaseConnected() ? "Yes" : "No");
    qWarning() << QString("Current Connections: %1").arg(stats.currentConnections);
    qWarning() << QString("Total Connections: %1").arg(stats.totalConnections);
    qWarning() << QString("Total Data Received: %1").arg(stats.totalDataReceived);
    qWarning() << "========================================";
}

int main(int argc, char *argv[]) {
    // Qt 애플리케이션 초기화
    QCoreApplication app(argc, argv);
    app.setApplicationName("ForensicServer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("ForensicLab");

    // 신호 핸들러 등록
    signal(SIGINT, signalHandler);   // Ctrl+C
    signal(SIGTERM, signalHandler);  // Terminate

    qInfo() << "====================================================";
    qInfo() << "🔥 FORENSIC SERVER STARTING";
    qInfo() << "====================================================";
    qInfo() << "Version: 3.0.0";
    qInfo() << "Build Date:" << __DATE__ << __TIME__;
    qInfo() << "Qt Version:" << QT_VERSION_STR;
    qInfo() << "";

    // 서버 설정 - config.ini 파일에서 읽기
    ForensicServer::ServerConfig config = ForensicServer::getDefaultConfig();

    // config.ini 파일 경로 설정
    QString configFilePath = QCoreApplication::applicationDirPath() + "/config.ini";

    // config.ini 파일 존재 확인
    if (!QFile::exists(configFilePath)) {
        qCritical() << "Config file not found:" << configFilePath;
        qCritical() << "Please copy config.ini.example to config.ini and fill in your settings.";
        return 1;
    }

    // QSettings로 설정 읽기
    QSettings settings(configFilePath, QSettings::IniFormat);

    // Network 설정
    config.listenAddress = settings.value("Network/ListenAddress", "0.0.0.0").toString();
    config.port = settings.value("Network/Port", 8443).toUInt();

    // Database 설정
    config.dbHost = settings.value("Database/Host", "localhost").toString();
    config.dbPort = settings.value("Database/Port", 5432).toInt();
    config.dbName = settings.value("Database/Name", "forensic_agent").toString();
    config.dbUser = settings.value("Database/User", "forensic_agent").toString();
    config.dbPassword = settings.value("Database/Password", "").toString();

    // BackendApi 설정
    config.enableBackendApi = settings.value("BackendApi/Enable", true).toBool();
    config.backendBaseUrl = settings.value("BackendApi/BaseUrl", "http://backend.unknownlite.com").toString();
    config.backendApiKey = settings.value("BackendApi/ApiKey", "").toString();
    config.backendTimeout = settings.value("BackendApi/Timeout", 30000).toInt();
    config.backendRetryCount = settings.value("BackendApi/RetryCount", 3).toInt();

    // Encryption 설정
    config.enableEncryption = settings.value("Encryption/Enable", true).toBool();
    config.encryptionKey = settings.value("Encryption/Key", "").toString();

    // 필수 값 검증
    if (config.dbPassword.isEmpty()) {
        qCritical() << "Database password is not set in config.ini";
        return 1;
    }
    if (config.enableBackendApi && config.backendApiKey.isEmpty()) {
        qCritical() << "Backend API key is not set in config.ini";
        return 1;
    }
    if (config.enableEncryption && config.encryptionKey.isEmpty()) {
        qCritical() << "Encryption key is not set in config.ini";
        return 1;
    }


    qInfo() << "🔧 Configuration loaded:";
    qInfo() << QString("   Network (Agents): %1:%2").arg(config.listenAddress).arg(config.port);
    qInfo() << QString("   Database: %1@%2:%3/%4")
                   .arg(config.dbUser)
                   .arg(config.dbHost)
                   .arg(config.dbPort)
                   .arg(config.dbName);
    qInfo() << QString("   Backend API: %1").arg(config.enableBackendApi ? config.backendBaseUrl : "Disabled");


    // ForensicServer 인스턴스 생성
    ForensicServer server(config);
    g_server = &server;

    // 서버 시작 (start()가 내부적으로 NetworkManager, DB, HttpApiHandler 등을 모두 시작)
    qInfo() << "";
    qInfo() << "🚀 Starting ForensicServer and all integrated services...";

    if (!server.start()) {
        qCritical() << "";
        qCritical() << "❌ FAILED TO START FORENSIC SERVER";
        qCritical() << "Check logs for detailed error information (e.g., port conflicts, DB issues).";
        qCritical() << "";
        return 1;
    }

    qInfo() << "✅ ForensicServer and integrated services started successfully.";
    qInfo() << "";


    // 10초 후 서버 상태 모니터링 (1회 실행)
    QTimer::singleShot(10000, [&server]() {
        monitorServerStatus(server);
    });

    // 60초마다 간단한 서버 상태 및 통계 출력
    QTimer* statusTimer = new QTimer(&app);
    QObject::connect(statusTimer, &QTimer::timeout, [&server]() {
        static int updateCount = 0;
        updateCount++;

        qInfo() << QString("\n[STATUS UPDATE #%1] %2")
                       .arg(updateCount)
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

        auto stats = server.getStats();
        qInfo() << QString("   Connections: %1 current, %2 total")
                       .arg(stats.currentConnections).arg(stats.totalConnections);
        qInfo() << QString("   Database: %1").arg(server.isDatabaseConnected() ? "Connected" : "Disconnected");
    });
    statusTimer->start(60000); // 60초마다

    qInfo() << "";
    qInfo() << "🎉 ===== FORENSIC SERVER READY ======";
    qInfo() << "🔴 Press Ctrl+C to stop the server gracefully.";
    qInfo() << "⚡ Waiting for Agent connections and API requests...";
    qInfo() << "";

    // Qt 이벤트 루프 시작
    int result = app.exec();

    // 정리 작업 (stop()이 모든 것을 처리)
    qInfo() << "";
    qInfo() << "🛑 Shutting down ForensicServer...";
    server.stop();
    qInfo() << "✅ ForensicServer shutdown complete.";
    qInfo() << "====================================================";

    return result;
}
