// databaseschema.h - 간소화된 테이블 스키마 (완전 교체 버전)
// client_info, forensic_info 테이블 기반의 단순한 구조

#ifndef DATABASESCHEMA_H
#define DATABASESCHEMA_H

#include "pch.h"
#include <QString>
#include <QStringList>
#include <QUuid>

// =================================================================
// DatabaseSchema - 간소화된 2개 테이블 구조
// client_info: PC 기본 정보 + recent_scan: Task 완료 시간 기준
// forensic_info: 포렌식 수집 데이터
// =================================================================

namespace DatabaseSchema {

// =================================================================
// 새로운 테이블 생성 쿼리
// =================================================================

// 클라이언트 정보 테이블 (client_info) - 핵심 정보만
const QString CREATE_CLIENT_INFO_TABLE = R"(
    CREATE TABLE IF NOT EXISTS client_info (
        pc_id VARCHAR(255) PRIMARY KEY,               -- MAC 기반 PC 식별자 (예: MAC_00-1A-2B-3C-4D-5E)
        pc_name VARCHAR(255),                         -- Windows PC 이름
        ip VARCHAR(45),                               -- 현재 IP 주소
        os VARCHAR(255),                              -- OS 버전 정보
        first_connect TIMESTAMP DEFAULT NOW(),        -- 최초 연결 시간
        last_connect TIMESTAMP DEFAULT NOW(),         -- 마지막 연결 시간
        recent_scan TIMESTAMP                         -- 마지막 Task 완료 시간 (Task 없으면 NULL)
    )
)";

// 포렌식 데이터 테이블 (forensic_info) - 수집 데이터만
const QString CREATE_FORENSIC_INFO_TABLE = R"(
    CREATE TABLE IF NOT EXISTS forensic_info (
        id SERIAL PRIMARY KEY,                        -- 자동 증가 ID
        pc_id VARCHAR(255) REFERENCES client_info(pc_id) ON DELETE CASCADE, -- PC 식별자 참조
        task_id VARCHAR(255) NOT NULL,                -- 수집 작업 고유 ID
        module_type VARCHAR(100) NOT NULL,            -- 수집 모듈 타입
        collection_time TIMESTAMP NOT NULL,           -- 데이터 수집 완료 시간
        file_size BIGINT DEFAULT 0,                   -- 수집된 데이터 크기 (bytes)
        json_data JSONB NOT NULL,                      -- 수집된 포렌식 데이터 (JSONB)
        created_at TIMESTAMP DEFAULT NOW()           -- 레코드 생성 시간
    )
)";

// =================================================================
// 인덱스 생성 쿼리
// =================================================================

const QString CREATE_CLIENT_INFO_INDEXES = R"(
    CREATE INDEX IF NOT EXISTS idx_client_info_pc_id ON client_info(pc_id);
    CREATE INDEX IF NOT EXISTS idx_client_info_recent_scan ON client_info(recent_scan);
    CREATE INDEX IF NOT EXISTS idx_client_info_last_connect ON client_info(last_connect);
)";

const QString CREATE_FORENSIC_INFO_INDEXES = R"(
    CREATE INDEX IF NOT EXISTS idx_forensic_info_pc_id ON forensic_info(pc_id);
    CREATE INDEX IF NOT EXISTS idx_forensic_info_task_id ON forensic_info(task_id);
    CREATE INDEX IF NOT EXISTS idx_forensic_info_module_type ON forensic_info(module_type);
    CREATE INDEX IF NOT EXIST S idx_forensic_info_created_at ON forensic_info(created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_forensic_info_collection_time ON forensic_info(collection_time DESC);
)";

// =================================================================
// PC 관리 쿼리 (client_info 테이블)
// =================================================================

// PC 등록/업데이트 (UPSERT 방식)
const QString UPSERT_CLIENT_INFO = R"(
    INSERT INTO client_info (pc_id, pc_name, ip, os, first_connect, last_connect)
    VALUES (:pc_id, :pc_name, :ip, :os, :first_connect, :last_connect)
    ON CONFLICT (pc_id)
    DO UPDATE SET
        pc_name = EXCLUDED.pc_name,
        ip = EXCLUDED.ip,
        os = EXCLUDED.os,
        last_connect = EXCLUDED.last_connect
    RETURNING pc_id
)";

// PC 정보 조회 (pc_id 기준)
const QString SELECT_CLIENT_INFO_BY_PC_ID = R"(
    SELECT pc_id, pc_name, ip, os, first_connect, last_connect, recent_scan
    FROM client_info
    WHERE pc_id = :pc_id
)";

// 전체 PC 목록 조회
const QString SELECT_ALL_CLIENT_INFO = R"(
    SELECT pc_id, pc_name, ip, os, first_connect, last_connect, recent_scan
    FROM client_info
    ORDER BY last_connect DESC
)";

// 마지막 연결 시간 업데이트
const QString UPDATE_CLIENT_LAST_CONNECT = R"(
    UPDATE client_info
    SET last_connect = :last_connect
    WHERE pc_id = :pc_id
)";

// recent_scan 업데이트 (Task 완료 시 호출)
const QString UPDATE_CLIENT_RECENT_SCAN = R"(
    UPDATE client_info
    SET recent_scan = :recent_scan
    WHERE pc_id = :pc_id
)";

// PC 정보 변경 감지 쿼리 (이전 값과 비교)
const QString SELECT_CLIENT_INFO_FOR_CHANGE_DETECTION = R"(
    SELECT pc_name, ip, os
    FROM client_info
    WHERE pc_id = :pc_id
)";

// =================================================================
// 포렌식 데이터 관리 쿼리 (forensic_test 테이블)
// =================================================================

// 포렌식 데이터 저장
const QString INSERT_FORENSIC_INFO = R"(
    INSERT INTO forensic_test (pc_id, task_id, module_type, collection_time, file_size, json_data)
    VALUES (:pc_id, :task_id, :module_type, :collection_time, :file_size, :json_data)
    RETURNING id
)";

// 포렌식 데이터 조회 (pc_id 기준)
const QString SELECT_FORENSIC_INFO_BY_PC_ID = R"(
    SELECT id, pc_id, task_id, module_type, collection_time, file_size, json_data
    FROM forensic_test
    WHERE pc_id = :pc_id
    ORDER BY collection_time DESC
    LIMIT :limit OFFSET :offset
)";

// 포렌식 데이터 조회 (task_id 기준)
const QString SELECT_FORENSIC_INFO_BY_TASK_ID = R"(
    SELECT id, pc_id, task_id, module_type, collection_time, file_size, json_data
    FROM forensic_test
    WHERE task_id = :task_id
    ORDER BY collection_time DESC
)";

// 포렌식 데이터 조회 (module_type 기준)
const QString SELECT_FORENSIC_INFO_BY_MODULE_TYPE = R"(
    SELECT id, pc_id, task_id, module_type, collection_time, file_size, json_data
    FROM forensic_test
    WHERE module_type = :module_type
    ORDER BY collection_time DESC
    LIMIT :limit OFFSET :offset
)";

// 최신 포렌식 데이터 조회
const QString SELECT_LATEST_FORENSIC_INFO = R"(
    SELECT id, pc_id, task_id, module_type, collection_time, file_size, json_data
    FROM forensic_test
    ORDER BY collection_time DESC
    LIMIT :limit OFFSET :offset
)";

// Task 완료 여부 확인 (Task ID로)
const QString CHECK_TASK_COMPLETION = R"(
    SELECT COUNT(*) as task_count
    FROM forensic_test
    WHERE task_id = :task_id
)";

// PC별 Task 완료 통계
const QString SELECT_PC_TASK_STATS = R"(
    SELECT
        pc_id,
        COUNT(*) as total_tasks,
        COUNT(DISTINCT module_type) as module_types,
        MAX(collection_time) as latest_collection,
        SUM(file_size) as total_size
    FROM forensic_test
    WHERE pc_id = :pc_id
    GROUP BY pc_id
)";

// =================================================================
// 유틸리티 함수
// =================================================================

// MAC 주소를 PC ID로 변환 (MAC_00-1A-2B-3C-4D-5E 형식)
inline QString generatePcIdFromMac(const QString& macAddress) {
    if (macAddress.isEmpty() || macAddress == "00:00:00:00:00:00") {
        return QString();
    }

    QString cleanMac = macAddress.toUpper().replace(":", "-");
    return QString("MAC_%1").arg(cleanMac);
}

// PC ID에서 MAC 주소 추출
inline QString extractMacFromPcId(const QString& pcId) {
    if (!pcId.startsWith("MAC_")) {
        return QString();
    }

    QString macPart = pcId.mid(4); // "MAC_" 제거
    return macPart.replace("-", ":");
}

// Task ID 생성 (UUID 기반)
inline QString generateTaskId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 데이터베이스 초기화 쿼리 목록
inline QStringList getInitializationQueries() {
    return {
        CREATE_CLIENT_INFO_TABLE,
        CREATE_FORENSIC_INFO_TABLE,
        CREATE_CLIENT_INFO_INDEXES,
        CREATE_FORENSIC_INFO_INDEXES
    };
}

} // namespace DatabaseSchema

#endif // DATABASESCHEMA_H
