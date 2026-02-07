#pragma once

#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

#include <libtorrent/session.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

namespace seekserve {

class AlertDispatcher {
public:
    AlertDispatcher() = default;
    ~AlertDispatcher();

    AlertDispatcher(const AlertDispatcher&) = delete;
    AlertDispatcher& operator=(const AlertDispatcher&) = delete;

    template<typename AlertType>
    void on(std::function<void(const AlertType&)> handler) {
        std::lock_guard lock(mu_);
        int type = AlertType::alert_type;
        handlers_[type] = [h = std::move(handler)](const lt::alert* a) {
            if (auto* typed = lt::alert_cast<AlertType>(a)) {
                h(*typed);
            }
        };
    }

    void start(lt::session& ses);
    void stop();

private:
    void run(lt::session& ses);

    std::unordered_map<int, std::function<void(const lt::alert*)>> handlers_;
    std::mutex mu_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex notify_mu_;
    std::condition_variable notify_cv_;
};

} // namespace seekserve
