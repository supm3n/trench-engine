#include "apps/common/net_store.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <sstream>

namespace antanta {

std::vector<NetSeatRecord> NetSeats_Parse(const std::string& text) {
    std::vector<NetSeatRecord> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        NetSeatRecord r;
        unsigned slot = 0;
        long long seen = 0;
        int used = 0;
        if (std::sscanf(line.c_str(), "%" SCNx64 " %u %" SCNx64 " %lld %n", &r.uid, &slot, &r.token, &seen, &used) <
                4 ||
            slot > 0xff || r.uid == 0) {
            continue;
        }
        r.slot = static_cast<std::uint8_t>(slot), r.seen = seen;
        r.address = used > 0 ? line.substr(static_cast<std::size_t>(used)) : std::string();
        out.push_back(std::move(r));
    }
    return out;
}

std::string NetSeats_Format(const std::vector<NetSeatRecord>& seats) {
    std::string out;
    for (const NetSeatRecord& r : seats) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%016" PRIx64 " %u %016" PRIx64 " %lld ", r.uid, unsigned{r.slot}, r.token,
                      static_cast<long long>(r.seen));
        out += buf + r.address + "\n";
    }
    return out;
}

void NetSeats_Put(std::vector<NetSeatRecord>& seats, const NetSeatRecord& r) {
    for (auto it = seats.begin(); it != seats.end();) it = it->uid == r.uid ? seats.erase(it) : it + 1;
    seats.insert(seats.begin(), r);
    if (seats.size() > 32) seats.resize(32);
}

const NetSeatRecord* NetSeats_Find(const std::vector<NetSeatRecord>& seats, std::uint64_t uid) {
    for (const NetSeatRecord& r : seats) {
        if (r.uid == uid) return &r;
    }
    return nullptr;
}

std::string NetAutosave_Name(std::uint64_t uid, std::uint32_t tick, std::uint32_t every) {
    const std::uint32_t n = every == 0 ? 1 : std::max<std::uint32_t>(1, tick / every);
    char buf[64];
    std::snprintf(buf, sizeof buf, "autosave-%016" PRIx64 "-%u.sav", uid, (n - 1) % 3 + 1);
    return buf;
}

bool NetAutosave_Is(const std::string& name) {
    // "autosave-" + 16 hex digits + "-" + 1..3 + ".sav"
    if (name.size() != 9 + 16 + 1 + 1 + 4 || name.compare(0, 9, "autosave-") != 0 || name[25] != '-') return false;
    for (std::size_t i = 9; i < 25; ++i) {
        const char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return name[26] >= '1' && name[26] <= '3' && name.compare(27, 4, ".sav") == 0;
}

NetAutosaveWriter::NetAutosaveWriter(Write write) : write_(std::move(write)) {
#ifndef __EMSCRIPTEN__
    thread_ = std::thread([this] { work(); });
#endif
}

NetAutosaveWriter::~NetAutosaveWriter() {
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
#endif
}

void NetAutosaveWriter::put(std::string path, sim::OwnSavePending save) {
#ifdef __EMSCRIPTEN__
    std::string err;
    const Bytes data = sim::OwnSave_Finish(save);
    if (write_(path, data, &err)) {
        ++written_;
    } else {
        error_ = path + ": " + err;
    }
#else
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.size() >= 2) queue_.pop_front();  // the game makes them faster than the disk takes them
        queue_.emplace_back(std::move(path), std::move(save));
    }
    cv_.notify_all();
#endif
}

void NetAutosaveWriter::flush() {
    std::unique_lock<std::mutex> lock(mu_);
    done_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

std::uint32_t NetAutosaveWriter::written() const {
    std::lock_guard<std::mutex> lock(mu_);
    return written_;
}

std::string NetAutosaveWriter::last_error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

void NetAutosaveWriter::work() {
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) return;  // stop_, nothing left
        auto job = std::move(queue_.front());
        queue_.pop_front();
        busy_ = true;
        lock.unlock();
        std::string err;
        const Bytes data = sim::OwnSave_Finish(job.second);
        const bool ok = !data.empty() && write_(job.first, data, &err);
        lock.lock();
        busy_ = false;
        if (ok) {
            ++written_;
        } else {
            error_ = job.first + ": " + (data.empty() ? std::string("the game could not be packed") : err);
        }
        done_.notify_all();
    }
}

}  // namespace antanta
