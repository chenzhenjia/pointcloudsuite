#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace pcv {
// Accounts only for optional acceleration storage, not formal business data.
class ProcessingBudget : public std::enable_shared_from_this<ProcessingBudget> {
public:
    explicit ProcessingBudget(size_t limit) : m_limit(limit) {}
    struct Lease {
        std::shared_ptr<ProcessingBudget> owner;
        size_t bytes;
        Lease(std::shared_ptr<ProcessingBudget> budget,size_t size):owner(std::move(budget)),bytes(size) {}
        Lease(const Lease &)=delete;
        Lease &operator=(const Lease &)=delete;
        ~Lease() { owner->m_used.fetch_sub(bytes); }
    };
    std::shared_ptr<Lease> acquire(size_t bytes) {
        auto owner=shared_from_this();
        size_t used=m_used.load();
        do { if(bytes>m_limit-used)return {}; }
        while(!m_used.compare_exchange_weak(used,used+bytes));
        try { return std::make_shared<Lease>(std::move(owner),bytes); }
        catch(...) { m_used.fetch_sub(bytes); throw; }
    }
    size_t used() const { return m_used.load(); }
private:
    const size_t m_limit;
    std::atomic_size_t m_used{0};
};
} // namespace pcv
