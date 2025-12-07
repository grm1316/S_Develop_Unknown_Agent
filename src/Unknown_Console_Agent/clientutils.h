// ClientUtils.h - 슬림화된 유틸리티 클래스
// PC 정보 수집, Owner_ID 처리는 ClientNetworkManager로 이동
// 콘솔 출력, JSON 검증, 공통 유틸리티만 유지

#ifndef CLIENTUTILS_H
#define CLIENTUTILS_H

#include "pch.h"
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>

// =================================================================
// ClientUtils - 순수 유틸리티 클래스
// 다른 모듈들이 공통으로 사용하는 기능들만 제공
// =================================================================

class ClientUtils {
public:
    // =================================================================
    // 콘솔 출력 함수들 (다른 수집 모듈들이 사용)
    // =================================================================

    /**
     * @brief 에러 메시지 출력 (빨간색)
     * @param title 제목
     * @param message 메시지 내용
     */
    static void showErrorMessage(const QString& title, const QString& message);

    /**
     * @brief 정보 메시지 출력 (파란색)
     * @param title 제목
     * @param message 메시지 내용
     */
    static void showInfoMessage(const QString& title, const QString& message);

    /**
     * @brief 성공 메시지 출력 (녹색)
     * @param title 제목
     * @param message 메시지 내용
     */
    static void showSuccessMessage(const QString& title, const QString& message);

    /**
     * @brief 경고 메시지 출력 (노란색)
     * @param title 제목
     * @param message 메시지 내용
     */
    static void showWarningMessage(const QString& title, const QString& message);

    /**
     * @brief 진행 상황 표시
     * @param message 진행 메시지
     * @param progress 진행률 (0-100, -1이면 무한 로딩)
     */
    static void showProgress(const QString& message, int progress = -1);

    /**
     * @brief CMD 콘솔 클리어
     */
    static void clearConsole();

    // =================================================================
    // JSON 유효성 검사 함수들 (공통 사용)
    // =================================================================

    /**
     * @brief JSON 숫자 값 유효성 검사 (NaN, Infinity 제거)
     * @param json 검사할 JSON 객체
     * @param path 현재 경로 (재귀용, 기본값 "")
     * @return 유효하면 true
     */
    static bool validateJsonNumbers(const QJsonObject& json, const QString& path = "");

    /**
     * @brief JSON 데이터 전체 유효성 검사
     * @param jsonData 검사할 JSON 데이터
     * @return 유효하면 true
     */
    static bool validateJsonData(const QJsonObject& jsonData);

    /**
     * @brief JSON 배열 내 숫자 유효성 검사
     * @param jsonArray 검사할 JSON 배열
     * @param path 현재 경로 (재귀용)
     * @return 유효하면 true
     */
    static bool validateJsonArrayNumbers(const QJsonArray& jsonArray, const QString& path = "");

    // =================================================================
    // 공통 문자열 처리 유틸리티
    // =================================================================

    /**
     * @brief 클라이언트 표시 이름 반환 (현재 폴더명 기반)
     * @return 정제된 클라이언트 이름
     */
    static QString getClientDisplayName();

    /**
     * @brief 현재 실행 파일의 폴더명 반환
     * @return 폴더명 (예: "홍길동_대리_PC")
     */
    static QString getCurrentFolderName();

    /**
     * @brief 파일 크기를 읽기 쉬운 형태로 변환
     * @param bytes 바이트 크기
     * @return 형식화된 문자열 (예: "1.5 MB")
     */
    static QString formatFileSize(qint64 bytes);

    /**
     * @brief 시간 간격을 읽기 쉬운 형태로 변환
     * @param startTime 시작 시간
     * @param endTime 종료 시간
     * @return 형식화된 문자열 (예: "2.5초")
     */
    static QString formatTimeInterval(const QDateTime& startTime, const QDateTime& endTime);

    /**
     * @brief 안전한 파일명 생성 (특수문자 제거)
     * @param fileName 원본 파일명
     * @return 안전한 파일명
     */
    static QString createSafeFileName(const QString& fileName);

    // =================================================================
    // JSON 파일 저장 유틸리티 (다른 모듈들이 사용)
    // =================================================================

    /**
     * @brief JSON 객체를 파일로 저장
     * @param jsonObject JSON 객체
     * @param filePath 파일 경로
     * @param indent 들여쓰기 여부 (기본값: true)
     * @return 성공하면 true
     */
    static bool saveJsonToFile(const QJsonObject& jsonObject, const QString& filePath, bool indent = true);

    /**
     * @brief 파일에서 JSON 객체 읽기
     * @param filePath 파일 경로
     * @return JSON 객체 (실패시 빈 객체)
     */
    static QJsonObject loadJsonFromFile(const QString& filePath);

    /**
     * @brief JSON 크기 계산 (바이트 단위)
     * @param jsonObject JSON 객체
     * @param compact 압축 형태 여부 (기본값: true)
     * @return 바이트 크기
     */
    static qint64 calculateJsonSize(const QJsonObject& jsonObject, bool compact = true);

    // =================================================================
    // 시스템 정보 조회 (간단한 것들만)
    // =================================================================

    /**
     * @brief 현재 시스템 언어 반환
     * @return 언어 코드 (예: "ko", "en")
     */
    static QString getCurrentLanguage();

    /**
     * @brief 현재 시스템 시간대 반환
     * @return 시간대 문자열
     */
    static QString getCurrentTimeZone();

    /**
     * @brief 애플리케이션 버전 반환
     * @return 버전 문자열
     */
    static QString getApplicationVersion();

private:
    // =================================================================
    // 내부 헬퍼 함수들
    // =================================================================

    /**
     * @brief 콘솔 색상 설정 (Windows)
     * @param color 색상 코드
     */
    static void setConsoleColor(int color);

    /**
     * @brief 콘솔 색상 초기화
     */
    static void resetConsoleColor();

    /**
     * @brief JSON 값이 유효한 숫자인지 확인
     * @param value JSON 값
     * @return 유효하면 true
     */
    static bool isValidNumber(const QJsonValue& value);
};

// =================================================================
// 상수 정의
// =================================================================

namespace ClientUtilsConstants {
// 콘솔 색상 코드 (Windows)
constexpr int COLOR_RED = 12;
constexpr int COLOR_GREEN = 10;
constexpr int COLOR_BLUE = 9;
constexpr int COLOR_YELLOW = 14;
constexpr int COLOR_WHITE = 15;
constexpr int COLOR_DEFAULT = 7;

// 파일 크기 단위
constexpr qint64 KB = 1024;
constexpr qint64 MB = KB * 1024;
constexpr qint64 GB = MB * 1024;

// 기본 설정
constexpr int DEFAULT_JSON_INDENT = 4;
constexpr int MAX_FILENAME_LENGTH = 255;
}

#endif // CLIENTUTILS_H
