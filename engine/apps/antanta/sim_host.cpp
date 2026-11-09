// The simulation's host in the game app (apps/antanta/sim_host.hpp, docs/sim-thread.md).
#include "apps/antanta/sim_host.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <cassert>
#include <string>
#include <utility>

#include "sim/trace.hpp"

namespace antanta {

// What nobody outside the sim reads (docs/sim-thread.md section 3): its caches, scratch and diagnostics, the trace's
// lists of the last step and the clock. While a picture is made, these are swapped out of the live state into the
// stash (each an O(1) swap), the rest is copied into the picture's buffer, then they are swapped back; so the picture
// has them empty and the live state keeps its own. A field added to GameState later is in the picture by default.
struct SimHost::Stash {
    std::vector<std::uint16_t> rng_table;
    std::vector<sim::PathRecord> path_cache;
    decltype(sim::GameState::flow_fields) flow_fields;
    decltype(sim::GameState::flow_open) flow_open, flow_weight;
    decltype(sim::GameState::flow_log) flow_log;
    decltype(sim::GameState::flow_stamp) flow_stamp;
    decltype(sim::GameState::land_region) land_region;
    decltype(sim::GameState::unit_cells) unit_cells;
    decltype(sim::GameState::house_cells) house_cells;
    decltype(sim::GameState::house_depth_maps) house_depth_maps;
    decltype(sim::Map::wave) wave;
    std::vector<sim::Command> orders, view_inputs;
    std::vector<sim::CommandExec> executed;
    std::vector<sim::SimEvent> events;
    decltype(sim::GameState::clock) clock;
    struct PlayerPart {
        decltype(sim::Player::history) history;
        decltype(sim::Player::hist_time) hist_time;
        decltype(sim::Player::script) script;
        decltype(sim::Player::commands) commands;
        decltype(sim::Player::zone_grid) zone_grid;
        decltype(sim::Player::free_ids) free_ids;
        decltype(sim::Player::free_slots) free_slots, freed_slots;
    };
    std::vector<PlayerPart> players;

    void swap(sim::GameState& s) {
        std::swap(s.rng_table, rng_table);
        std::swap(s.path_cache, path_cache);
        std::swap(s.flow_fields, flow_fields);
        std::swap(s.flow_open, flow_open);
        std::swap(s.flow_weight, flow_weight);
        std::swap(s.flow_log, flow_log);
        std::swap(s.flow_stamp, flow_stamp);
        std::swap(s.land_region, land_region);
        std::swap(s.unit_cells, unit_cells);
        std::swap(s.house_cells, house_cells);
        std::swap(s.house_depth_maps, house_depth_maps);
        std::swap(s.map.wave, wave);
        std::swap(s.orders, orders);
        std::swap(s.view_inputs, view_inputs);
        std::swap(s.executed, executed);
        std::swap(s.events, events);
        std::swap(s.clock, clock);
        if (players.size() < s.players.size()) players.resize(s.players.size());
        for (std::size_t p = 0; p < s.players.size(); ++p) {
            sim::Player& pl = s.players[p];
            PlayerPart& k = players[p];
            std::swap(pl.history, k.history);
            std::swap(pl.hist_time, k.hist_time);
            std::swap(pl.script, k.script);
            std::swap(pl.commands, k.commands);
            std::swap(pl.zone_grid, k.zone_grid);
            std::swap(pl.free_ids, k.free_ids);
            std::swap(pl.free_slots, k.free_slots);
            std::swap(pl.freed_slots, k.freed_slots);
        }
    }
};

SimHost::SimHost() : stash_(std::make_unique<Stash>()) {}
SimHost::~SimHost() { shutdown(); }

void SimHost::set_files(FileLoader load, const std::vector<BasicParams>* params) {
    load_ = std::move(load);
    params_ = params;
}

void SimHost::set_mode(Mode m, bool lockstep) {
    assert(on_main() && !host_owns_);
    mode_ = m, lockstep_ = lockstep;
}

const sim::GameState* SimHost::view() const {
    assert(on_main());
    if (!game_.state) return nullptr;
    return host_owns_ && viewed_ >= 0 ? &ring_[static_cast<std::size_t>(viewed_)]->state : &*game_.state;
}

SimGame& SimHost::game() {
    assert(on_main() && !host_owns_);  // the main thread changes the live state only while it holds the game
    return game_;
}

// Under mutex_ with the sim thread out of a tick: the grants not started give their orders to the next tick. A network
// game's ticks are the server's frames: they stay and run after the hold.
void SimHost::drop_grants() {
    if (!grants_.empty() && grants_.front().net) return;
    for (TickGrant& g : grants_) {
        for (std::vector<sim::Command>& o : g.orders) game_.orders.push_back(std::move(o));
    }
    grants_.clear();
}

void SimHost::hold() {
    assert(on_main());
    std::unique_lock<std::mutex> lock(mutex_);
    parked_ = true;
    to_main_.wait(lock, [&] { return !running_; });  // at most the tick in progress
    drop_grants();
    host_owns_ = false;
}

void SimHost::replace(std::unique_ptr<sim::GameState> s) {
    hold();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const TickRecord& r : records_) {
            if (r.slot >= 0) slots_[static_cast<std::size_t>(r.slot)] = Slot::Free;
        }
        records_.clear();
        grants_.clear();  // a network game's ticks of the old game
        stopped_ = false;
    }
    game_.orders.clear();
    if (s) {
        game_.state = std::move(*s);
    } else {
        game_.state.reset();
    }
}

// Under mutex_.
bool SimHost::free_slot_there() const {
    for (const Slot s : slots_) {
        if (s == Slot::Free) return true;
    }
    return false;
}

// Under mutex_.
int SimHost::free_slot() {
    for (std::size_t k = 0; k < slots_.size(); ++k) {
        if (slots_[k] != Slot::Free) continue;
        if (!ring_[k]) ring_[k] = std::make_unique<SimPicture>();
        slots_[k] = Slot::Filling;
        return static_cast<int>(k);
    }
    assert(false);  // the sim thread waits for a free slot; the main thread takes every record before its next grant
    return -1;
}

// Outside mutex_: the slot is Filling, nobody else reads it.
void SimHost::make_picture(int slot) {
    const Uint64 t0 = SDL_GetTicksNS();
    sim::GameState& live = *game_.state;
    stash_->swap(live);
    ring_[static_cast<std::size_t>(slot)]->state = live;
    stash_->swap(live);
    const double ms = static_cast<double>(SDL_GetTicksNS() - t0) / 1e6;
    std::lock_guard<std::mutex> lock(mutex_);
    copy_ms_ += ms, ++copies_;
}

// Under mutex_ (the main thread).
void SimHost::show(int slot) {
    if (slot < 0) return;
    assert(slots_[static_cast<std::size_t>(slot)] == Slot::Ready);
    if (viewed_ >= 0) slots_[static_cast<std::size_t>(viewed_)] = Slot::Free;
    slots_[static_cast<std::size_t>(slot)] = Slot::Viewed;
    viewed_ = slot;
}

namespace {

// A tick that opens a game message window (mission_events: MissionMessage, not log_only, not end, with a text): the
// unthreaded loop runs no tick after it in that frame (docs/sim-thread.md 4, "Game messages").
bool opens_message(const TickRecord& r) {
    for (const sim::SimEvent& e : r.events) {
        if (e.kind == sim::SimEvent::Kind::MissionMessage && !e.mission.log_only && !e.mission.end && !e.text.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

void SimHost::tick_and_picture(TickGrant& g, TickRecord& r) {
    run_tick(g, r);
    if (r.slot >= 0) make_picture(r.slot);
}

// The game comes back to the host: the view goes on from a picture of the live state.
void SimHost::hand_over() {
    int k;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        k = free_slot();
    }
    make_picture(k);
    std::lock_guard<std::mutex> lock(mutex_);
    assert(records_.empty());  // held(app) and the frame take every record before a grant; the sim is parked
    slots_[static_cast<std::size_t>(k)] = Slot::Ready;
    show(k);
    host_owns_ = true;
}

void SimHost::resume() {
    if (mode_ != Mode::Thread || host_owns_ || !game_.state) return;
    hand_over();
    std::lock_guard<std::mutex> lock(mutex_);
    parked_ = false;
    to_sim_.notify_one();
}

void SimHost::grant(TickGrant g) {
    assert(on_main());
    if (mode_ == Mode::Off) {
        TickRecord r;
        run_tick(g, r);
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(std::move(r));
        return;
    }
    if (!host_owns_) hand_over();
    if (mode_ == Mode::Thread && !thread_) {
        if (start_thread()) SDL_Log("the sim thread runs the ticks%s", lockstep_ ? " (lockstep)" : "");
        if (!thread_) {  // no thread: the ticks run here as with the pictures, the game goes on
            SDL_Log("the sim thread could not start (%s): the ticks run on the main thread", SDL_GetError());
            mode_ = Mode::Pictures;
        }
    }
    if (mode_ == Mode::Pictures) {
        TickRecord r;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            r.slot = free_slot();
        }
        tick_and_picture(g, r);
        std::lock_guard<std::mutex> lock(mutex_);
        slots_[static_cast<std::size_t>(r.slot)] = Slot::Ready;
        records_.push_back(std::move(r));
        return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopped_ && records_.empty()) stopped_ = false;  // the main thread saw the message's tick and goes on
    parked_ = false;
    grants_.push_back(std::move(g));
    to_sim_.notify_one();
    if (lockstep_) to_main_.wait(lock, [&] { return (grants_.empty() && !running_) || stopped_; });
}

// The sim thread with an 8 MB stack (docs/sim-thread.md 4): SDL's thread on the desktop; on the threaded web page a
// pthread (the web build's SDL has no threads), one more worker of the page's pool (engine/CMakeLists.txt).
bool SimHost::start_thread() {
    constexpr int kStack = 8 << 20;
#ifdef __EMSCRIPTEN__
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStack);
    pthread_t t;
    const int e = pthread_create(
        &t, &attr,
        [](void* self) -> void* {
            thread_main(self);
            return nullptr;
        },
        this);
    pthread_attr_destroy(&attr);
    if (e != 0) {
        SDL_SetError("pthread_create gave %d", e);
        return false;
    }
    thread_ = t;
#else
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                           reinterpret_cast<void*>(&SimHost::thread_main));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "sim");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, kStack);
    thread_ = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
#endif
    return static_cast<bool>(thread_);
}

int SimHost::thread_main(void* self) {
    static_cast<SimHost*>(self)->sim_loop();
    return 0;
}

void SimHost::sim_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        to_sim_.wait(lock, [&] { return quit_ || (!parked_ && !stopped_ && !grants_.empty() && free_slot_there()); });
        if (quit_) return;
        TickGrant g = std::move(grants_.front());
        grants_.pop_front();
        TickRecord r;
        r.slot = free_slot();
        running_ = true;
        lock.unlock();
        tick_and_picture(g, r);
        const bool stop = !r.net && opens_message(r);
        lock.lock();
        slots_[static_cast<std::size_t>(r.slot)] = Slot::Ready;
        records_.push_back(std::move(r));
        running_ = false;
        if (stop) stopped_ = true, drop_grants();
        to_main_.notify_all();
    }
}

bool SimHost::take(TickRecord* r) {
    assert(on_main());
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty()) return false;
    *r = std::move(records_.front());
    records_.pop_front();
    show(r->slot);
    to_sim_.notify_one();  // a slot may be free now
    return true;
}

int SimHost::in_flight() {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(grants_.size() + records_.size()) + (running_ ? 1 : 0);
}

DrawBlend* SimHost::blend_of(const TickRecord& r) {
    if (!r.blend) return nullptr;
    return r.slot >= 0 ? &ring_[static_cast<std::size_t>(r.slot)]->blend : &blend_;
}

void SimHost::close_files() {
    hold();
    if (game_.trace) std::fclose(game_.trace), game_.trace = nullptr;
    if (game_.commands) std::fclose(game_.commands), game_.commands = nullptr;
}

void SimHost::shutdown() {
    if (!thread_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        to_sim_.notify_one();
    }
#ifdef __EMSCRIPTEN__
    pthread_join(*thread_, nullptr);  // after the tick in progress
    thread_.reset();
#else
    SDL_WaitThread(thread_, nullptr);
    thread_ = nullptr;
#endif
}

double SimHost::copy_ms() {
    std::lock_guard<std::mutex> lock(mutex_);
    return copy_ms_;
}

std::uint32_t SimHost::copies() {
    std::lock_guard<std::mutex> lock(mutex_);
    return copies_;
}

// One step: the view inputs (D lines), the selection's inputs, then the UI's orders for this tick (O lines), each
// through its text form as antanta-headless will read it, then step(). On the sim thread with --sim-thread on.
void SimHost::run_tick(TickGrant& g, TickRecord& r) {
    assert(mode_ != Mode::Thread || !on_main());  // with the thread, the ticks are the sim thread's alone
    sim::GameState& s = *game_.state;
    if (g.net) {  // slice S3: as NetGame::run steps it (the whole map's view inputs, the frame's orders)
        std::vector<std::string> warn;  // not logged, as NetGame::run
        r.net = NetGame_RunTick(s, *g.net, load_, *params_, &warn);
        r.tick = s.tick;
        r.events.swap(s.events);
        return;
    }
    if (g.blend) {  // where everything is before the step (the window draws the moving things between two ticks)
        draw_blend_snapshot(r.slot >= 0 ? ring_[static_cast<std::size_t>(r.slot)]->blend : blend_, s);
        r.blend = true;
    }
    std::vector<sim::Command> now;
    if (g.view == TickGrant::View::WholeMap) {
        now = view_inputs(view_box_whole_map(s), s);
    } else if (g.view == TickGrant::View::Box) {
        now = view_inputs(g.box, s);
    }
    std::vector<std::string> warn;
    load_house_depth(load_, *params_, now, s, &warn);
    for (const std::string& w : warn) SDL_Log("view input: %s", w.c_str());

    if (auto c = ui::select_houses(s, g.sel)) {
        c->tick = s.tick + 1;
        now.push_back(std::move(*c));
    }
    if (auto c = ui::select_summary(s, g.sel)) {  // slice M9: the mission condition 13's selection
        c->tick = s.tick + 1;
        now.push_back(std::move(*c));
    }
    for (std::vector<sim::Command>& o : g.orders) game_.orders.push_back(std::move(o));
    for (sim::Command& c : ui::take_tick_orders(s, game_.orders)) now.push_back(std::move(c));
    std::vector<sim::Command> parsed;
    std::string text;
    for (const sim::Command& c : now) {
        const bool view = c.kind == sim::Command::Kind::HouseDepth;
        const std::string line = view ? sim::Command_PrintView(c) : sim::Command_Print(c);
        std::string err;
        auto back = view ? sim::Command_ParseView(line, &err) : sim::Command_Parse(line, &err);
        if (!back) {
            SDL_Log("dropped an order that does not survive its text form (%s): %s", err.c_str(), line.c_str());
            continue;
        }
        parsed.push_back(std::move(*back));
        text += line + '\n';
    }
    if (game_.commands && !text.empty()) {
        std::fwrite(text.data(), 1, text.size(), game_.commands);
        std::fflush(game_.commands);  // the session log survives a crash or a killed window
    }
    const std::uint32_t enter = s.tick;
    sim::step(s, game_.lua.get(), parsed);
    for (; game_.lua && game_.lua_logged < sim::ScriptVm_Errors(*game_.lua).size(); ++game_.lua_logged) {
        SDL_Log("script: %s", sim::ScriptVm_Errors(*game_.lua)[game_.lua_logged].c_str());
    }
    if (game_.trace) {
        const std::string t = game_.trace_hashes ? sim::trace_state_hash(s)
                                                 : sim::trace_orders(s) + sim::trace_view(s) + sim::trace_commands(s) +
                                                       sim::trace_tick(s, enter) + sim::trace_state_hash(s);
        std::fwrite(t.data(), 1, t.size(), game_.trace);
    }
    r.tick = s.tick;
    r.events.swap(s.events);  // taken out, so a paused step (which does not empty the list) does not raise them again
}

}  // namespace antanta
