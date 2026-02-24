# Forensic Server

Windows 기반 포렌식 데이터 수집 및 분석 시스템의 서버 및 에이전트 컴포넌트입니다.

## 프로젝트 개요

ForensicServer는 원격 클라이언트로부터 포렌식 데이터를 수집하고 중앙 집중식으로 관리하는 서버 애플리케이션입니다. Qt 프레임워크 기반 C++로 작성되었으며, PostgreSQL 데이터베이스와 Python FastAPI 백엔드와 통합됩니다.

### 주요 기능

- 6가지 포렌식 데이터 수집 지원
  - USB 연결 이력
  - 브라우저 사용 기록
  - Prefetch 파일
  - LNK 바로가기 파일
  - 삭제된 파일 (MFT 분석)
  - 메신저 대화 내용

- PC 등록 및 관리
  - MAC 주소 기반 PC 식별
  - 소유자 인증 (analyst ID)
  - PC 변경 감지

- 보안 기능
  - AES-256 암호화
  - SSL/TLS 지원
  - 안전한 데이터 전송

- 데이터베이스 통합
  - PostgreSQL 기반 데이터 저장
  - JSONB 형식 지원
  - 트랜잭션 관리

## 시스템 요구사항

### 필수 요구사항

- Windows 10 이상
- Visual Studio 2019 이상 (MSVC 컴파일러)
- Qt 6.5 이상
- PostgreSQL 14 이상
- CMake 3.19 이상

### 권장 사양

- CPU: 4코어 이상
- RAM: 8GB 이상
- 저장공간: 100GB 이상 (데이터 수집량에 따라 조정)

## 설치 방법

### 1. 저장소 클론
```bash
git clone https://github.com/your-username/forensic-server.git
cd forensic-server
```

### 2. 설정 파일 생성
```bash
# config.ini.example을 config.ini로 복사
cp config.ini.example config.ini

# config.ini 파일 편집
notepad config.ini
```

### 3. config.ini 설정

config.ini 파일에서 다음 항목을 실제 값으로 수정:
```ini
[Database]
Password=YOUR_DB_PASSWORD_HERE

[BackendApi]
ApiKey=YOUR_BACKEND_API_KEY_HERE

[Encryption]
Key=YOUR_ENCRYPTION_KEY_HERE
```

### 4. PostgreSQL 데이터베이스 설정
```sql
-- 데이터베이스 및 사용자 생성
CREATE DATABASE forensic_agent;
CREATE USER forensic_agent WITH PASSWORD 'your_password';
GRANT ALL PRIVILEGES ON DATABASE forensic_agent TO forensic_agent;
```

### 5. CMake 빌드
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### 6. 실행 파일 위치

빌드 완료 후 실행 파일 위치:
```
build/bin/release/ForensicServer.exe
```

## 사용 방법

### 서버 시작
```bash
cd build/bin/release
./ForensicServer.exe
```

### 서버 중지

- Ctrl+C 입력으로 안전하게 종료

### 로그 확인

- 콘솔 출력으로 실시간 로그 확인
- 설정에 따라 파일 로그 저장 가능

## 프로젝트 구조
```
forensic-server/
├── main.cpp                    # 메인 진입점
├── ForensicServer.h/cpp        # 서버 메인 클래스
├── NetworkManager.h/cpp        # 네트워크 통신 관리
├── DatabaseManager.h/cpp       # 데이터베이스 관리
├── BackendApiClient.h/cpp      # 백엔드 API 클라이언트
├── HttpApiHandler.h/cpp        # HTTP API 핸들러
├── CryptoUtils.h/cpp           # 암호화 유틸리티
├── backend_types.h             # 백엔드 타입 정의
├── databaseschema.h            # 데이터베이스 스키마
├── pch.h/cpp                   # 미리 컴파일된 헤더
├── CMakeLists.txt              # CMake 빌드 설정
├── config.ini.example          # 설정 파일 템플릿
├── config.ini                  # 실제 설정 (Git 제외)
└── README.md                   # 프로젝트 문서
```

## 기술 스택

### 핵심 프레임워크
- Qt 6.5+ (Core, Network, Sql)
- C++17 표준

### 데이터베이스
- PostgreSQL 14+
- JSONB 데이터 타입

### 네트워크
- Windows Socket API
- Qt Network (QTcpServer, QTcpSocket)
- SSL/TLS 지원

### 암호화
- Windows CryptoAPI
- AES-256 암호화

### 빌드 도구
- CMake 3.19+
- MSVC 컴파일러

## 설정 옵션

config.ini 파일의 주요 설정 항목:

### Network
- `ListenAddress`: 서버 바인딩 주소 (기본: 0.0.0.0)
- `Port`: 서버 포트 (기본: 8443)

### Database
- `Host`: PostgreSQL 호스트
- `Port`: PostgreSQL 포트 (기본: 5432)
- `Name`: 데이터베이스 이름
- `User`: 데이터베이스 사용자
- `Password`: 데이터베이스 비밀번호

### BackendApi
- `Enable`: 백엔드 API 사용 여부
- `BaseUrl`: 백엔드 서버 URL
- `ApiKey`: API 인증 키
- `Timeout`: 요청 타임아웃 (ms)
- `RetryCount`: 재시도 횟수

### Encryption
- `Enable`: 암호화 사용 여부
- `Key`: AES-256 암호화 키

## 보안 고려사항

### 중요 정보 관리
- config.ini 파일은 절대 Git에 커밋하지 마세요
- config.ini.example만 GitHub에 공유됩니다
- 암호화 키는 32자 이상 권장

### 데이터베이스 보안
- 강력한 비밀번호 사용
- 필요시 SSL 연결 활성화
- 정기적인 백업 수행

### 네트워크 보안
- 방화벽 규칙 설정
- SSL/TLS 인증서 사용 권장
- 내부 네트워크 사용 권장

## 문제 해결

### 빌드 오류

**Qt를 찾을 수 없음:**
```bash
# CMake에 Qt 경로 지정
cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.5.0/msvc2019_64"
```

**PostgreSQL 라이브러리 누락:**
- Qt SQL PostgreSQL 드라이버 설치 확인

### 실행 오류

**config.ini not found:**
- config.ini.example을 config.ini로 복사했는지 확인

**Database connection failed:**
- PostgreSQL 서버 실행 여부 확인
- config.ini의 데이터베이스 설정 확인

**Port already in use:**
- config.ini에서 다른 포트 번호로 변경

## 기여 방법

1. Fork 프로젝트
2. 기능 브랜치 생성 (`git checkout -b feature/amazing-feature`)
3. 변경사항 커밋 (`git commit -m 'Add amazing feature'`)
4. 브랜치에 Push (`git push origin feature/amazing-feature`)
5. Pull Request 생성

## 라이선스

이 프로젝트는 비공개 프로젝트입니다.

## 연락처

프로젝트 관련 문의사항이 있으시면 이슈를 등록해주세요.
