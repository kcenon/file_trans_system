// Standalone sanitizer diagnostic: no file_trans_system or Asio dependency.
// Record access intervals with relaxed atomics, which do not order errno access.
#include <cerrno>
#include <cstdint>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdio>
struct record { uintptr_t address; uint64_t begin, end; };
int main() {
    constexpr int workers=8, rounds=500;
    std::vector<record> observations(workers*rounds);
    std::atomic<uint64_t> sequence{};
    std::vector<std::thread> owners;
    for (int w=0; w<workers; ++w) owners.emplace_back([&,w] {
        for (int i=0; i<rounds; ++i) {
            std::thread child([&,w,i] {
                auto& record=observations[w*rounds+i];
                record.address=reinterpret_cast<uintptr_t>(&errno);
                record.begin=sequence.fetch_add(1,std::memory_order_relaxed);
                for (int k=0;k<10;++k) {
                    volatile int* slot = &errno;
                    *slot = k;
                    volatile int observed = *slot;
                    (void)observed;
                }
                record.end=sequence.fetch_add(1,std::memory_order_relaxed);
            });
            child.join();
        }
    });
    for(auto& owner:owners)owner.join();
    for(const auto& r:observations)std::printf("%llu,%llu,%llu\n",static_cast<unsigned long long>(r.address),static_cast<unsigned long long>(r.begin),static_cast<unsigned long long>(r.end));
}
