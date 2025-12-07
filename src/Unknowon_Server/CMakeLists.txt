cmake_minimum_required(VERSION 3.19)
project(Forensic_Server LANGUAGES CXX)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# 🔥 AWS SDK 비활성화 설정
# =============================================================================
set(AWS_SDK_FOUND FALSE)
set(DISABLE_AWS_SDK TRUE)

message(STATUS "=== 🔥 Forensic Server (PostgreSQL Only) ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "⚠️  AWS S3 Integration: DISABLED")

# =============================================================================
# Qt6 패키지 찾기
# =============================================================================
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()
message(STATUS "✅ Qt Version: ${Qt6_VERSION}")

# =============================================================================
# AWS SDK 검색 건너뛰기 (비활성화됨)
# =============================================================================
if(NOT DISABLE_AWS_SDK)
    # AWS SDK 검색 로직들은 비활성화로 인해 실행되지 않음
    message(STATUS "🔍 Searching for AWS SDK...")
    # ... AWS SDK 검색 코드들 (실행되지 않음) ...
else()
    message(STATUS "🚫 AWS SDK search skipped (manually disabled)")
    set(AWS_SEARCH_METHODS "manually_disabled")
endif()

# =============================================================================
# 소스 파일 정의 (S3Manager 제외)
# =============================================================================
set(SERVER_SOURCES
    # 🔧 Core 컴포넌트
    NetworkManager.cpp
    NetworkManager.h
    databasemanager.cpp
    databasemanager.h
    forensicserver.cpp
    forensicserver.h

    # 📁 유틸리티
    pch.cpp
    pch.h
    main.cpp

    # 🔐 암호화 유틸리티 (새로 추가)
    CryptoUtils.cpp
    CryptoUtils.h
    # 📝 참고: S3Manager.cpp/h 파일들은 프로젝트에 유지되지만 빌드에서 제외됨
)

# S3Manager 소스 제외 (파일은 삭제하지 않음)
message(STATUS "⚠️  S3Manager sources excluded from build (files preserved)")

# =============================================================================
# 서버 실행 파일 생성
# =============================================================================
add_executable(Forensic_Server ${SERVER_SOURCES}
    databaseschema.h
    httpapihandler.h httpapihandler.cpp
    backend_types.h
    backendapiclient.h
    backendapiclient.cpp
    cryptoutils.h cryptoutils.cpp
    config.ini.example
    config.ini

)

# =============================================================================
# Qt 라이브러리 링크
# =============================================================================
target_link_libraries(Forensic_Server PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

# =============================================================================
# AWS SDK 없이 빌드 설정
# =============================================================================
target_compile_definitions(Forensic_Server PRIVATE
    AWS_SDK_ENABLED=0
    USE_S3_STORAGE=0
)
message(STATUS "⚠️  Building without AWS SDK support")

# =============================================================================
# Windows 전용 설정
# =============================================================================
if(WIN32)
    target_link_libraries(Forensic_Server PRIVATE
        ws2_32      # Winsock2
        wevtapi     # Windows Event Log API
        advapi32    # Advanced Windows API
        shell32     # Shell API
        version     # Version Info API
        ntdll       # NT DLL
        crypt32     # Cryptography API
        secur32     # Security API
    )
target_link_libraries(Forensic_Server PRIVATE Qt6::Core)
target_link_libraries(Forensic_Server PRIVATE Qt6::Core)
target_link_libraries(Forensic_Server PRIVATE Qt6::Core)

    # 콘솔 애플리케이션으로 설정 (서버 로그 확인용)
    set_property(TARGET Forensic_Server PROPERTY WIN32_EXECUTABLE FALSE)

    # UTF-8 및 유니코드 지원
    target_compile_definitions(Forensic_Server PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
    )

    message(STATUS "✅ Windows-specific libraries configured")
endif()

# =============================================================================
# 컴파일 옵션 설정
# =============================================================================
if(MSVC)
    target_compile_options(Forensic_Server PRIVATE
        /W3         # Warning Level 3
        /utf-8      # UTF-8 Support
        /permissive-# Strict C++ Standards
    )

    # 프리컴파일 헤더 사용
    target_precompile_headers(Forensic_Server PRIVATE pch.h)

    message(STATUS "✅ MSVC compiler options configured")
else()
    target_compile_options(Forensic_Server PRIVATE
        -Wall -Wextra -Wpedantic
    )
    message(STATUS "✅ GCC/Clang compiler options configured")
endif()

# =============================================================================
# Debug/Release 구성별 설정
# =============================================================================
target_compile_definitions(Forensic_Server PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE QT_QML_DEBUG _DEBUG>
    $<$<CONFIG:Release>:NDEBUG QT_NO_DEBUG>
)

# 출력 디렉토리 설정
set_target_properties(Forensic_Server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin/debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/release"
    OUTPUT_NAME "ForensicServer"
)

# =============================================================================
# Qt 자동 배포 (Windows 개발환경)
# =============================================================================
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Forensic_Server POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:Forensic_Server> --release --no-translations
            COMMENT "🚀 Deploying Qt libraries..."
        )
        message(STATUS "✅ windeployqt found: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(STATUS "⚠️  windeployqt not found - manual Qt DLL deployment required")
    endif()
endif()

# =============================================================================
# 빌드 완료 후 정보 출력
# =============================================================================
add_custom_command(TARGET Forensic_Server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "🎉 ===== BUILD COMPLETED ====="
    COMMAND ${CMAKE_COMMAND} -E echo "📋 Configuration Summary:"
    COMMAND ${CMAKE_COMMAND} -E echo "   🔧 Qt Version: ${Qt6_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E echo "   ☁️  AWS SDK: ❌ Disabled (${AWS_SEARCH_METHODS})"
    COMMAND ${CMAKE_COMMAND} -E echo "   💾 Database: PostgreSQL Support"
    COMMAND ${CMAKE_COMMAND} -E echo "   🔐 Network: TLS/SSL Support"
    COMMAND ${CMAKE_COMMAND} -E echo "   📦 Storage: Database Only (S3 Disabled)"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "🚀 Executable: $<TARGET_FILE:Forensic_Server>"
    COMMAND ${CMAKE_COMMAND} -E echo "📁 Output Dir: $<TARGET_FILE_DIR:Forensic_Server>"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "💡 S3Manager files preserved but excluded from build"
    COMMAND ${CMAKE_COMMAND} -E echo "💡 To re-enable AWS S3: Set DISABLE_AWS_SDK to FALSE"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    VERBATIM
)

# =============================================================================
# 최종 빌드 정보 요약
# =============================================================================
message(STATUS "")
message(STATUS "🎯 ===== CONFIGURATION SUMMARY =====")
message(STATUS "Target Name: Forensic_Server (PostgreSQL Only)")
message(STATUS "Qt Version: ${Qt6_VERSION}")
message(STATUS "AWS SDK: DISABLED")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Active Sources: ${SERVER_SOURCES}")
message(STATUS "Excluded: S3Manager (files preserved)")
message(STATUS "=====================================")
message(STATUS "")
