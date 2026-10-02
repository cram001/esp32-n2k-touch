#pragma once
#include <cstdint>

// Owned exclusively by the HTTP startup worker. Requests are coalesced before
// reaching this policy, so AP events cannot create overlapping servers.
struct LocalServerRetry {
    static constexpr unsigned limit=10;
    static constexpr int64_t delay_us=2000000;
    unsigned attempts=0;
    bool active=false;
    bool listening=false;
    int64_t due_us=0;
    void request(int64_t now) {
        if(active || listening)return;
        attempts=0;active=true;due_us=now;
    }
    bool due(int64_t now)const {return active && now>=due_us;}
    void complete(bool success,int64_t now) {
        ++attempts;listening=success;
        active=!success && attempts<limit;
        due_us=now+delay_us;
    }
};

enum class StartupHealthDecision { Wait, Confirm, Reject };
inline StartupHealthDecision startup_health_decision(bool prerequisites,bool listening,
                                                     bool ui_healthy,int64_t elapsed_us) {
    if(!prerequisites)return StartupHealthDecision::Reject;
    if(elapsed_us<5000000)return StartupHealthDecision::Wait;
    if(listening && ui_healthy)return StartupHealthDecision::Confirm;
    return elapsed_us>=25000000?StartupHealthDecision::Reject:StartupHealthDecision::Wait;
}
