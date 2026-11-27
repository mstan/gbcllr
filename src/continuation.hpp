#pragma once
#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>
#if defined(_WIN32) && !defined(GBCLLR_PORTABLE_CABLE)
#include <windows.h>
#ifdef GBCLLR_FCONTEXT_CABLE
#include <memory>
#endif
#elif (defined(GBCLLR_UCONTEXT_CABLE) || defined(GBCLLR_FCONTEXT_CABLE)) && defined(__linux__) && defined(__GLIBC__) && defined(__x86_64__) && !defined(__ILP32__) && !defined(GBCLLR_PORTABLE_CABLE)
#define GBCLLR_CONTEXT_UCONTEXT
#include <memory>
#include <ucontext.h>
#ifdef GBCLLR_FCONTEXT_CABLE
#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#endif
#if defined(__SANITIZE_ADDRESS__)
#define GBCLLR_CONTEXT_ASAN
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GBCLLR_CONTEXT_ASAN
#endif
#endif
#ifdef GBCLLR_CONTEXT_ASAN
#include <sanitizer/common_interface_defs.h>
#endif
#else
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace gbcllr::detail {
#ifdef GBCLLR_FCONTEXT_CABLE
#ifdef _WIN32
#define GBCLLR_CONTEXT_ABI __attribute__((ms_abi))
#else
#define GBCLLR_CONTEXT_ABI __attribute__((sysv_abi))
#endif
struct ContextTransfer {void* context;void* data;};
using ContextEntry=void (GBCLLR_CONTEXT_ABI *)(ContextTransfer);
extern "C" ContextTransfer GBCLLR_CONTEXT_ABI gbcllr_context_jump(void*,void*) noexcept;
extern "C" void* GBCLLR_CONTEXT_ABI gbcllr_context_make(void*,size_t,ContextEntry) noexcept;
#endif
// Stackful CPU continuation. The portable implementation transfers exclusive
// ownership with a mutex; it never runs the consoles concurrently. Windows
// fibers and optional Linux contexts avoid an OS thread wake per T cycle.
// Contexts stay on their creating thread. No suspended stack is serialized.
class Continuation {
    std::function<void()> body_;
    std::exception_ptr error_;
    bool quit_=false;
#if defined(_WIN32) && !defined(GBCLLR_PORTABLE_CABLE)
    struct Host {
        void* fiber;
        unsigned users;
        bool converted;
    };
    inline static thread_local Host host_{};
    void* fiber_=nullptr;
#ifdef GBCLLR_FCONTEXT_CABLE
    static constexpr size_t stack_size=1024*1024;
    std::unique_ptr<unsigned char[]> stack_;
    void* fast_context_=nullptr;
    void* fast_host_=nullptr;
    bool fast_=false,started_=false;
    static bool fast_available() noexcept {
#if defined(__SANITIZE_ADDRESS__)
        return false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
        return false;
#endif
#endif
        // Read policy once during construction. This assembly does not manage
        // CET shadow stacks. Only the documented dynamic-registration API
        // restriction is harmless here: this static assembly never calls
        // SetProcessDynamicEHContinuationTargets or
        // SetProcessDynamicEnforcedCetCompatibleRanges. Every other/unknown
        // flag retains OS fibers. No mitigation or system setting is changed.
        auto saved_error=GetLastError();
        using Query=BOOL (WINAPI *)(HANDLE,PROCESS_MITIGATION_POLICY,PVOID,SIZE_T);
        auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcessMitigationPolicy"));
        PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY policy{};
        constexpr DWORD registration_only=1u<<8; // CetDynamicApisOutOfProcOnly
        bool available=query&&query(GetCurrentProcess(),ProcessUserShadowStackPolicy,&policy,sizeof policy)&&
            (policy.Flags&~registration_only)==0;
        SetLastError(saved_error);
        return available;
    }
    static void GBCLLR_CONTEXT_ABI fast_entry(ContextTransfer transfer) noexcept {
        auto& self=*static_cast<Continuation*>(transfer.data);
        self.fast_host_=transfer.context;
        self.loop();
    }
#endif
    static VOID WINAPI entry(void* p) {static_cast<Continuation*>(p)->loop();}
    void loop() noexcept {
        while(!quit_) {
            try {body_();}catch(...){error_=std::current_exception();}
            yield();
        }
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_) {gbcllr_context_jump(fast_host_,this);std::terminate();}
#endif
        yield();
    }
public:
    const char* backend() const noexcept {
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_)return "windows-boost-fcontext";
#endif
        return "windows-fiber";
    }
    explicit Continuation(std::function<void()> body):body_(std::move(body)) {
#ifdef GBCLLR_FCONTEXT_CABLE
        fast_=fast_available();
        if(fast_) {
            stack_=std::make_unique<unsigned char[]>(stack_size);
            fast_context_=gbcllr_context_make(stack_.get()+stack_size,stack_size,fast_entry);
            if(!fast_context_)throw std::runtime_error("cannot create cable CPU context");
            return;
        }
#endif
        if(!host_.users) {
            host_.converted=!IsThreadAFiber();
            host_.fiber=host_.converted?ConvertThreadToFiberEx(nullptr,FIBER_FLAG_FLOAT_SWITCH):GetCurrentFiber();
            if(!host_.fiber)throw std::runtime_error("cannot create cable scheduler fiber");
        }
        ++host_.users;
        fiber_=CreateFiberEx(65536,1024*1024,FIBER_FLAG_FLOAT_SWITCH,entry,this);
        if(!fiber_) {
            if(!--host_.users&&host_.converted)ConvertFiberToThread();
            throw std::runtime_error("cannot create cable CPU fiber");
        }
    }
    ~Continuation() {
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_) {if(started_) {quit_=true;resume();}return;}
#endif
        // The owner must drain the current instruction before destruction.
        DeleteFiber(fiber_);
        if(!--host_.users&&host_.converted)ConvertFiberToThread();
    }
    void yield() noexcept {
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_) {fast_host_=gbcllr_context_jump(fast_host_,this).context;return;}
#endif
        SwitchToFiber(host_.fiber);
    }
    void resume() {
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_) {started_=true;fast_context_=gbcllr_context_jump(fast_context_,this).context;}
        else
#endif
        SwitchToFiber(fiber_);
        if(error_)std::rethrow_exception(std::exchange(error_,{}));
    }
#elif defined(GBCLLR_CONTEXT_UCONTEXT)
    static constexpr size_t stack_size=1024*1024;
    std::unique_ptr<unsigned char[]> stack_{new unsigned char[stack_size]};
    ucontext_t context_{},host_{};
    inline static thread_local Continuation* entering_=nullptr;
    bool started_=false;
#ifdef GBCLLR_FCONTEXT_CABLE
    void* fast_context_=nullptr;
    void* fast_host_=nullptr;
    bool fast_=false;
    static bool fast_available() noexcept {
        // Query once when creating the continuation. Active shadow stacks use
        // glibc's context implementation; the pinned assembly needs a separate
        // shadow-stack allocation in that mode. Unknown status also falls back.
#ifdef ARCH_SHSTK_STATUS
        unsigned long features=0;auto saved_errno=errno;
        auto result=syscall(SYS_arch_prctl,ARCH_SHSTK_STATUS,&features);
        auto failure=errno;errno=saved_errno;
        return result==0?features==0:(failure==EINVAL||failure==EOPNOTSUPP);
#else
        return false;
#endif
    }
    static void __attribute__((sysv_abi)) fast_entry(ContextTransfer transfer) noexcept {
        auto& self=*static_cast<Continuation*>(transfer.data);
        self.fast_host_=transfer.context;
        self.begin();
    }
#endif
#ifdef GBCLLR_CONTEXT_ASAN
    void* fake_stack_=nullptr;
    void* host_fake_stack_=nullptr;
    const void* host_stack_=nullptr;
    size_t host_stack_size_=0;
#endif
    static void entry() noexcept {
        // No pointer varargs: makecontext only promises integer arguments.
        entering_->begin();
    }
    void begin() noexcept {
#ifdef GBCLLR_CONTEXT_ASAN
        __sanitizer_finish_switch_fiber(nullptr,&host_stack_,&host_stack_size_);
#endif
        loop();
    }
    void loop() noexcept {
        while(!quit_) {
            try {body_();}catch(...){error_=std::current_exception();}
            yield();
        }
#ifdef GBCLLR_CONTEXT_ASAN
        // The child will never resume; release its sanitizer fake stack.
        __sanitizer_start_switch_fiber(nullptr,host_stack_,host_stack_size_);
#endif
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_) {gbcllr_context_jump(fast_host_,this);std::terminate();}
#endif
        setcontext(&host_);
        std::terminate();
    }
public:
    const char* backend() const noexcept {
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_)return "boost-fcontext";
#endif
        return "glibc-ucontext";
    }
    explicit Continuation(std::function<void()> body):body_(std::move(body)) {
#ifdef GBCLLR_FCONTEXT_CABLE
        fast_=fast_available();
        if(fast_) {
            fast_context_=gbcllr_context_make(stack_.get()+stack_size,stack_size,fast_entry);
            return;
        }
#endif
        if(getcontext(&context_)!=0)throw std::runtime_error("cannot create cable CPU context");
        context_.uc_stack.ss_sp=stack_.get();context_.uc_stack.ss_size=stack_size;
        context_.uc_link=&host_;
        makecontext(&context_,entry,0);
    }
    ~Continuation() {
        // The owner has drained the guest instruction. Unwind the idle loop
        // on its own stack before freeing it, including sanitizer bookkeeping.
        if(started_) {quit_=true;resume();}
    }
    void yield() noexcept {
#ifdef GBCLLR_CONTEXT_ASAN
        __sanitizer_start_switch_fiber(&fake_stack_,host_stack_,host_stack_size_);
#endif
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_)fast_host_=gbcllr_context_jump(fast_host_,this).context;
        else
#endif
        if(swapcontext(&context_,&host_)!=0)std::terminate();
#ifdef GBCLLR_CONTEXT_ASAN
        __sanitizer_finish_switch_fiber(fake_stack_,&host_stack_,&host_stack_size_);
#endif
    }
    void resume() {
        entering_=this;started_=true;
#ifdef GBCLLR_CONTEXT_ASAN
        __sanitizer_start_switch_fiber(&host_fake_stack_,stack_.get(),stack_size);
#endif
#ifdef GBCLLR_FCONTEXT_CABLE
        if(fast_)fast_context_=gbcllr_context_jump(fast_context_,this).context;
        else
#endif
        if(swapcontext(&host_,&context_)!=0)std::terminate();
#ifdef GBCLLR_CONTEXT_ASAN
        __sanitizer_finish_switch_fiber(host_fake_stack_,nullptr,nullptr);
#endif
        if(error_)std::rethrow_exception(std::exchange(error_,{}));
    }
#else
    std::mutex mutex_;
    std::condition_variable changed_;
    bool running_=false;
    std::thread thread_;
    void loop() noexcept {
        std::unique_lock lock(mutex_);
        changed_.wait(lock,[&]{return running_||quit_;});
        while(!quit_) {
            lock.unlock();
            try {body_();}catch(...){error_=std::current_exception();}
            yield();
            lock.lock();
        }
    }
public:
    const char* backend() const noexcept {return "portable-thread";}
    explicit Continuation(std::function<void()> body):body_(std::move(body)),thread_([this]{loop();}) {}
    ~Continuation() {
        {std::lock_guard lock(mutex_);quit_=true;changed_.notify_all();}
        thread_.join();
    }
    void yield() noexcept {
        std::unique_lock lock(mutex_);running_=false;changed_.notify_all();
        changed_.wait(lock,[&]{return running_||quit_;});
    }
    void resume() {
        std::unique_lock lock(mutex_);running_=true;changed_.notify_all();
        changed_.wait(lock,[&]{return !running_;});
        if(error_)std::rethrow_exception(std::exchange(error_,{}));
    }
#endif
};
}
