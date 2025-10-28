// RemoteZoneProfiler.h — FIXED + MULTI-THREAD SUPPORT
#define TRACY_ENABLE
#include "tracy/Tracy.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <queue>
#include <condition_variable>
#include <thread>

#include "tracy/TracyC.h"

// ---------- Утилиты чтения (unchanged, optimal) ----------
static inline uint8_t readByte(const std::vector<unsigned char>& data, size_t& pos) {
    return data[pos++];
}

static inline int32_t readInt(const std::vector<unsigned char>& data, size_t& pos) {
    int32_t v; std::memcpy(&v, &data[pos], 4); pos += 4; return v;
}

static inline int64_t readLong(const std::vector<unsigned char>& data, size_t& pos) {
    int64_t v; std::memcpy(&v, &data[pos], 8); pos += 8; return v;
}

static inline std::string readJavaStringAsUtf8(const std::vector<unsigned char>& data, size_t& pos) {
    const int len = readInt(data, pos);
    std::string out; out.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        const uint16_t c = uint16_t(data[pos]) | (uint16_t(data[pos + 1]) << 8);
        pos += 2;
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

class RemoteZoneProfiler {
public:
    // Публичный API (unchanged)
    void handlePacket(const std::vector<unsigned char>& data);

    // NEW: Graceful shutdown — join all workers
    void shutdown();

    ~RemoteZoneProfiler() { shutdown(); }  // RAII

private:
    // ---------- Action: serialized packet ----------
    struct Action {
        uint8_t type;
        std::string name;  // for type=1; ignored otherwise (move-optimized)
    };

    // ---------- Per-JavaThread context (actor-like) ----------
    struct ThreadContext {
        int64_t java_id;
        std::mutex queue_mtx;
        std::condition_variable queue_cv;
        std::queue<Action> actions;  // MT-safe via mtx
        bool shutdown = false;

        // PER-WORKER LOCAL (no lock! single-thread access)
        struct ActiveZone {
            std::unique_ptr<tracy::ScopedZone> zone;
            std::string name;  // debug-only
        };
        std::vector<ActiveZone> stack;  // thread-local stack

        std::unique_ptr<std::thread> worker;
    };

    std::unordered_map<int64_t, std::unique_ptr<ThreadContext>> contexts_;
    mutable std::mutex contexts_mtx_;  // protects map (rare contention)

    // ---------- Core logic ----------
    ThreadContext* get_or_create_context(int64_t java_id);

    static void worker_func(RemoteZoneProfiler* self, ThreadContext* ctx);

    void process_action(ThreadContext* ctx, Action&& action);

    void zone_start(ThreadContext* ctx, std::string&& name);
    void zone_end(ThreadContext* ctx);
};

// ---------- IMPLEMENTATION ----------
inline void RemoteZoneProfiler::handlePacket(const std::vector<unsigned char>& data) {
    size_t pos = 0;
    const uint8_t type = readByte(data, pos);
    const std::string name = readJavaStringAsUtf8(data, pos);
    const int64_t java_id = readLong(data, pos);

    // Get/create context → dispatch async
    ThreadContext* ctx = get_or_create_context(java_id);
    {
        std::lock_guard<std::mutex> lk(ctx->queue_mtx);
        ctx->actions.emplace(Action{type, std::move(name)});  // zero-copy move
        ctx->queue_cv.notify_one();
    }
}

inline RemoteZoneProfiler::ThreadContext* RemoteZoneProfiler::get_or_create_context(int64_t java_id) {
    std::lock_guard<std::mutex> lk(contexts_mtx_);
    auto& ctx_ptr = contexts_[java_id];
    if (!ctx_ptr) {
        // Lazy spawn: O(1) amortized
        ctx_ptr = std::make_unique<ThreadContext>();
        ctx_ptr->java_id = java_id;
        ctx_ptr->worker = std::make_unique<std::thread>(worker_func, this, ctx_ptr.get());
    }
    return ctx_ptr.get();
}

inline void RemoteZoneProfiler::worker_func(RemoteZoneProfiler* self, ThreadContext* ctx) {
    // Tracy: unique timeline + name
    const std::string thread_name = "Thread#" + std::to_string(ctx->java_id);
    tracy::SetThreadName(thread_name.c_str());

    while (true) {
        std::unique_lock<std::mutex> lk(ctx->queue_mtx);
        ctx->queue_cv.wait(lk, [&ctx] { return !ctx->actions.empty() || ctx->shutdown; });
        if (ctx->shutdown && ctx->actions.empty()) {
            break;  // Clean exit
        }
        auto action = std::move(ctx->actions.front());
        ctx->actions.pop();
        lk.unlock();  // Unlock BEFORE heavy work

        // Process unlocked (thread-local)
        self->process_action(ctx, std::move(action));
    }
}

inline void RemoteZoneProfiler::process_action(ThreadContext* ctx, Action&& action) {
    switch (action.type) {
        case 1:  // Start
            zone_start(ctx, std::move(action.name));
            break;
        case 0:  // End
            zone_end(ctx);
            break;
        case 3:  // FrameMark
            FrameMark;
            break;
        default:
            // Ignore invalid (no log — perf)
            break;
    }
}

inline void RemoteZoneProfiler::zone_start(ThreadContext* ctx, std::string&& name) {
    constexpr const char* src = "JavaRemote";
    constexpr size_t src_len = 9;

    // Optimized: runtime source + thread-local func (name already in thread_name)
    auto z = std::make_unique<tracy::ScopedZone>(
        /*line*/ 0,
        /*source*/ src, src_len,
        /*function*/ name.c_str(), name.size(),  // Reuse 'name' as func for granularity
        /*name*/ name.c_str(), name.size(),
        /*color*/ 0,
        /*depth*/ -1,
        /*active*/ true
    );

    ctx->stack.push_back({std::move(z), std::move(name)});  // Move everywhere
}

inline void RemoteZoneProfiler::zone_end(ThreadContext* ctx) {
    if (!ctx->stack.empty()) {
        ctx->stack.pop_back();  // RAII: ~ScopedZone → Tracy ZoneEnd
    }
    // Ignore mismatch (robust)
}

inline void RemoteZoneProfiler::shutdown() {
    std::lock_guard<std::mutex> lk(contexts_mtx_);
    for (auto& [id, ctx_ptr] : contexts_) {
        auto& ctx = *ctx_ptr;
        {
            std::lock_guard q_lk(ctx.queue_mtx);
            ctx.shutdown = true;
            ctx.queue_cv.notify_all();
        }
        if (ctx.worker) {
            ctx.worker->join();
            ctx.worker.reset();
        }
    }
    contexts_.clear();
}