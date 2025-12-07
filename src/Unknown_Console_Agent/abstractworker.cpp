#include "pch.h"
#include "abstractworker.h"

using namespace std;

AbstractWorker::AbstractWorker() : pcRegistrationStatus_(PC_NOT_REGISTERED) {  // 🆕 PC 등록 상태 초기화
    if(!Init()) throw runtime_error("Failed to create abstractworker");
}

AbstractWorker::~AbstractWorker() {
    End();
    if (controlThread_.joinable()) controlThread_.join();
}

bool AbstractWorker::Init() {
    try {
        status_ = STATUS_IDLE;
        pcRegistrationStatus_ = PC_NOT_REGISTERED;  // 🆕 PC 등록 상태 초기화
        controlThread_ = std::thread(&AbstractWorker::ControlWorker, this);
    }catch(const exception& e){
        WarningMsg(string("<init> ").append(e.what()));
        return false;
    }

    return true;
}

void AbstractWorker::WarningMsg(const std::string msg) const {
    cerr<<"[AbstractWorker]"<<msg<<endl;
}

void AbstractWorker::ErrorMsg(const std::string msg) const {
    cerr<<"[AbstractWorker]"<<msg<<endl;
    exit(1);
}

std::shared_lock<std::shared_mutex> AbstractWorker::DataSync() {
    return shared_lock<shared_mutex>(dataMtx_);
}

void AbstractWorker::Play() {
    cv_.notify_all();
    statusRunning_ = true;
}

void AbstractWorker::Pause() {
    unique_lock<mutex> lck(statusMtx_);
    cv_.wait(lck, [&] {
        return statusRunning_;
    });
    statusRunning_ = false;
}

void AbstractWorker::End() {
    if (status_ == STATUS_ENDED) return;
    status_ = STATUS_ENDED;
    Play();
}

void AbstractWorker::Run() {
    if (status_ == STATUS_RUNNING) return;
    status_ = STATUS_RUNNING;
    Play();
}

void AbstractWorker::RunOneShot() {
    if (status_ == STATUS_ONE_SHOT) return;
    status_ = STATUS_ONE_SHOT;
    Play();
}

void AbstractWorker::Stop() {
    if (status_ == STATUS_STOPPED) return;
    status_ = STATUS_STOPPED;
}

void AbstractWorker::Error() {
    if(status_ == STATUS_ERROR) return;
    status_ = STATUS_ERROR;
    Play();
}

AbstractWorker::status AbstractWorker::GetState() {
    return status_;
}

// 🆕 PC 등록 상태 관리 메서드들
AbstractWorker::PCRegistrationStatus AbstractWorker::GetPCRegistrationStatus() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(pcRegistrationMutex_));
    return pcRegistrationStatus_;
}

void AbstractWorker::SetPCRegistrationStatus(PCRegistrationStatus status) {
    std::lock_guard<std::mutex> lock(pcRegistrationMutex_);
    pcRegistrationStatus_ = status;

    const char* statusNames[] = {"NOT_REGISTERED", "REGISTERING", "REGISTERED", "FAILED"};
    WarningMsg(string("[PC Registration] Status changed to: ") + statusNames[status]);
}

bool AbstractWorker::EnsurePCRegistration() {
    // 이미 등록된 경우
    if (GetPCRegistrationStatus() == PC_REGISTERED) {
        return true;
    }

    // 등록 중인 경우 잠시 대기
    if (GetPCRegistrationStatus() == PC_REGISTERING) {
        WarningMsg("[PC Registration] Registration in progress, waiting...");
        return false;
    }

    try {
        WarningMsg("[PC Registration] Starting PC registration process...");
        SetPCRegistrationStatus(PC_REGISTERING);

        // 1. 네트워크 매니저 초기화
        if (!InitializeNetworkManager()) {
            WarningMsg("[PC Registration] Failed to initialize network manager");
            SetPCRegistrationStatus(PC_REGISTRATION_FAILED);
            return false;
        }

        // 2. 네트워크 연결 확인
        if (!IsNetworkConnected()) {
            WarningMsg("[PC Registration] Network not connected");
            SetPCRegistrationStatus(PC_REGISTRATION_FAILED);
            return false;
        }

        // 3. PC 등록 수행
        if (!RegisterPC()) {
            WarningMsg("[PC Registration] Failed to register PC");
            SetPCRegistrationStatus(PC_REGISTRATION_FAILED);
            return false;
        }

        // 4. 등록 상태 확인
        if (!IsPCRegistered()) {
            WarningMsg("[PC Registration] PC registration verification failed");
            SetPCRegistrationStatus(PC_REGISTRATION_FAILED);
            return false;
        }

        SetPCRegistrationStatus(PC_REGISTERED);
        WarningMsg("[PC Registration] PC registration completed successfully");
        return true;

    } catch (const exception& e) {
        WarningMsg(string("[PC Registration] Exception: ") + e.what());
        SetPCRegistrationStatus(PC_REGISTRATION_FAILED);
        return false;
    }
}

bool AbstractWorker::IsPCReady() const {
    return (GetPCRegistrationStatus() == PC_REGISTERED) && IsNetworkConnected();
}

void AbstractWorker::ControlWorker() {
    static auto lastPCRegistrationAttempt = std::chrono::steady_clock::now();

    while (1) {
        try {
            switch (status_) {
            case STATUS_IDLE: {
                Pause();
                break;
            }
            case STATUS_ONE_SHOT: {
                // 🆕 PC 등록 우선 처리
                if (EnsurePCRegistration()) {
                    WarningMsg("[AbstractWorker] PC registered, starting data collection...");
                    Register();  // 포렌식 데이터 수집
                } else {
                    WarningMsg("[AbstractWorker] PC registration required before data collection");
                }

                Stop();  // 원샷 모드 완료
                break;
            }
            case STATUS_RUNNING: {
                std::this_thread::sleep_for(std::chrono::milliseconds(CONTROL_TICK));

                // 🆕 PC 등록 상태 확인 및 처리
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPCRegistrationAttempt).count();

                PCRegistrationStatus pcStatus = GetPCRegistrationStatus();

                if (pcStatus == PC_REGISTERED) {
                    // PC 등록 완료 - 정상적인 데이터 수집 수행
                    Register();
                } else if (pcStatus == PC_REGISTRATION_FAILED && timeSinceLastAttempt >= PC_REGISTRATION_RETRY_INTERVAL) {
                    // 등록 실패 - 재시도 간격 후 다시 시도
                    WarningMsg("[AbstractWorker] Retrying PC registration...");
                    SetPCRegistrationStatus(PC_NOT_REGISTERED);
                    lastPCRegistrationAttempt = now;
                } else if (pcStatus == PC_NOT_REGISTERED) {
                    // 등록 안됨 - 등록 시도
                    if (timeSinceLastAttempt >= PC_REGISTRATION_RETRY_INTERVAL) {
                        EnsurePCRegistration();
                        lastPCRegistrationAttempt = now;
                    }
                }
                // PC_REGISTERING 상태일 때는 대기

                break;
            }
            case STATUS_STOPPED: {
                Pause();
                break;
            }
            case STATUS_ENDED: {
                return;
                break;
            }
            case STATUS_ERROR: {
                break;
            }
            default:
                break;
            }
        }
        catch (const exception& e) {
            ErrorMsg(string("<WorkerFunc> ").append(e.what()));
        }
    }
}
