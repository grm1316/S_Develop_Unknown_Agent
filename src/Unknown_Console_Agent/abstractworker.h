#ifndef ABSTRACTWORKER_H
#define ABSTRACTWORKER_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>

// 🆕 전방 선언 추가
class ClientNetworkManager;

class AbstractWorker
{
public:
    enum status {
        STATUS_IDLE = 0,
        STATUS_ONE_SHOT,
        STATUS_RUNNING,
        STATUS_STOPPED,
        STATUS_ENDED,
        STATUS_ERROR,
        STATUS_MAX
    };

    // 🆕 PC 등록 상태 열거형 추가
    enum PCRegistrationStatus {
        PC_NOT_REGISTERED = 0,    // PC 등록 안됨
        PC_REGISTERING = 1,       // PC 등록 중
        PC_REGISTERED = 2,        // PC 등록 완료
        PC_REGISTRATION_FAILED = 3 // PC 등록 실패
    };

private:
    std::thread controlThread_{};
    std::mutex statusMtx_{};
    bool statusRunning_{};
    std::shared_mutex dataMtx_{};
    std::condition_variable cv_{};

    bool Init();

    //control
    void Play();
    void Pause();
    void End();

protected:
    status status_{};

    // 🆕 PC 등록 상태 관리
    PCRegistrationStatus pcRegistrationStatus_;
    std::mutex pcRegistrationMutex_;

    virtual void WarningMsg(const std::string msg) const;
    virtual void ErrorMsg(const std::string msg) const;

    // 🆕 PC 등록 관련 순수 가상 함수들
    virtual bool InitializeNetworkManager() = 0;        // 네트워크 매니저 초기화
    virtual bool RegisterPC() = 0;                       // PC 등록 수행
    virtual bool IsNetworkConnected() const = 0;         // 네트워크 연결 상태 확인
    virtual bool IsPCRegistered() const = 0;             // PC 등록 상태 확인

    // 기존 순수 가상 함수
    virtual void Register() = 0;                         // 포렌식 데이터 등록

    std::shared_lock<std::shared_mutex> DataSync();

    //control
    void Error();
    void ControlWorker();

    // 🆕 PC 등록 상태 관리 메서드들
    PCRegistrationStatus GetPCRegistrationStatus() const;
    void SetPCRegistrationStatus(PCRegistrationStatus status);
    bool EnsurePCRegistration();  // PC 등록 보장

public:
    static constexpr uint64_t CONTROL_TICK = 100;
    static constexpr int PC_REGISTRATION_RETRY_INTERVAL = 5000;  // 🆕 PC 등록 재시도 간격 (5초)

    explicit AbstractWorker();
    virtual ~AbstractWorker();

    status GetState();

    void Run();
    void RunOneShot();
    void Stop();

    // 🆕 PC 등록 상태 조회 (외부 인터페이스)
    bool IsPCReady() const;  // PC 등록 완료 및 네트워크 연결 확인
};

#endif // ABSTRACTWORKER_H
