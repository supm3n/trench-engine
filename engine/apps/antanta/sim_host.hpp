// The simulation's host in the game app (docs/sim-thread.md): it holds the live game (the state, a Lua mission's
// script, the --trace and --commands-out files, the orders a tick left for the next one) and is the one place that
// runs its ticks. The rest of the app reads the state only through view(), a const GameState, and changes it outside a
// tick only through game() while it holds the game (hold(): loads, new games, saves, the end statistics).
// Slice S0: one place for the ticks. Slice S1: the picture: after each tick a copy of the state without the sim's own
// caches goes into a ring of three buffers, and the drawing and the work after the tick read that copy. Slice S2: the
// ticks on a thread of their own (--sim-thread on, the setting), the main thread grants them and takes their records.
// Slice S3: a network game's ticks too (NetTick: the frame from the server and what the tick needs from the client).
// Slice S4: the threaded web page too (a pthread there).
#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <pthread.h>
#endif

#include "apps/common/net_game.hpp"
#include "apps/common/view_input.hpp"
#include "formats/params.hpp"
#include "render/live.hpp"
#include "script/lua_manager.hpp"
#include "sim/state.hpp"
#include "ui/hud.hpp"

struct SDL_Thread;

namespace antanta {

// The game the host holds.
struct SimGame {
    std::optional<sim::GameState> state;
    sim::ScriptVmPtr lua;           // slice M5: a Lua mission's script, opened by start_new_mission
    std::size_t lua_logged = 0;     // its errors written to the log so far
    std::FILE* trace = nullptr;     // --trace
    bool trace_hashes = false;      // --trace-hashes: only each tick's G line
    std::FILE* commands = nullptr;  // --commands-out
    // The orders granted to the ticks and not taken yet (ui::take_tick_orders may leave a ui_order for the next tick).
    std::deque<std::vector<sim::Command>> orders;
};

// One tick the main thread asks for: what the main thread knows that the tick reads.
struct TickGrant {
    std::deque<std::vector<sim::Command>> orders;  // the orders queued since the last grant, in their order
    ui::Selection sel;                             // the selection, for select_houses and select_summary
    // The view inputs (docs/view-dependence.md): none, every house drawn, or the houses of the camera's box.
    enum class View : std::uint8_t { None, WholeMap, Box } view = View::None;
    ViewBox box{};
    bool blend = false;  // the game window: where the units and shells were before the step (draw_blend_snapshot)
    std::optional<NetTick> net;  // a network game's tick (NetGame::next_tick): its frame instead of the fields above
};

// What a tick gives back to the main thread.
struct TickRecord {
    std::uint32_t tick = 0;             // the tick it ran (the state's tick after the step)
    std::vector<sim::SimEvent> events;  // the sounds, messages and marks the step raised
    int slot = -1;                      // its picture in the ring (-1: none, the view is the live state)
    bool blend = false;                 // its blend snapshot is in blend_of()
    std::optional<NetTickResult> net;   // a network game's tick: its trace lines, hash, autosave, step time
};

// The copy of the state the main thread draws (docs/sim-thread.md section 3) and the blend snapshot of the tick
// before it.
struct SimPicture {
    sim::GameState state;
    DrawBlend blend;
};

class SimHost {
  public:
    enum class Mode : std::uint8_t {
        Off,       // the view is the live state, as before the sim thread
        Pictures,  // the view is a picture of the last tick, the ticks on the main thread (slice S1)
        Thread,    // the ticks on the sim thread (slice S2)
    };

    SimHost();
    ~SimHost();

    // The game files and the BasicParams the view inputs' depth maps are loaded with; both outlive the host's games.
    void set_files(FileLoader load, const std::vector<BasicParams>* params);
    // While the main thread holds the game (a load, before the first tick). Lockstep (Thread): each grant waits for its
    // tick, as the batch runs and the test scripts need (docs/sim-thread.md section 6).
    void set_mode(Mode m, bool lockstep);
    Mode mode() const { return mode_; }
    bool lockstep() const { return lockstep_; }

    // The state to draw and to build orders from; null without a game.
    const sim::GameState* view() const;
    // The live game, for the main thread while it holds it (hold()).
    SimGame& game();
    // Takes the game back for the main thread: the tick in progress ends (Thread), the grants not started yet go, their
    // orders kept for the next tick (a network game's stay: the server fixed them), and no tick runs until the next
    // grant. The records of the ticks that ran stay for take().
    void hold();
    // The live state replaced (a load, a new game) or dropped: the grants, the records and the orders not taken yet
    // go with the old one.
    void replace(std::unique_ptr<sim::GameState> s);

    // Asks for one tick with the grant's inputs (Off, Pictures, lockstep: it runs at once); its record waits for
    // take(). A grant while the main thread holds the game gives it back to the host, the picture made at once from the
    // live state. A grant also lets the ticks go on after a message stop when every record was taken.
    void grant(TickGrant g);
    // A network game held with ticks still granted (their frames stay, hold()): they go on without a new grant.
    void resume();
    // The next tick record in tick order; false when there is none (never waits). The view becomes its picture.
    bool take(TickRecord* r);
    // The ticks granted whose records were not taken yet.
    int in_flight();
    // The record's blend snapshot (r.blend), the main thread's until its next take.
    DrawBlend* blend_of(const TickRecord& r);

    // The --trace and --commands-out files closed (a new game opens its own); the game held first.
    void close_files();
    // The sim thread ended (the app's end).
    void shutdown();

    // The pictures' copy time, for --bench-frame: the sum in milliseconds and the count.
    double copy_ms();
    std::uint32_t copies();

  private:
    struct Stash;  // the fields the picture leaves out, while it is made
    enum class Slot : std::uint8_t { Free, Filling, Ready, Viewed };

    void run_tick(TickGrant& g, TickRecord& r);
    void hand_over();
    void tick_and_picture(TickGrant& g, TickRecord& r);
    bool free_slot_there() const;
    int free_slot();
    void make_picture(int slot);
    void show(int slot);
    void drop_grants();
    bool start_thread();
    static int thread_main(void* self);
    void sim_loop();

    // Slice S5: the ownership asserts (Debug and ASan builds): the calls above are the main thread's, and with the
    // sim thread no tick runs on the main thread.
    bool on_main() const { return std::this_thread::get_id() == main_id_; }
    const std::thread::id main_id_ = std::this_thread::get_id();  // the host is made on the main thread

    Mode mode_ = Mode::Off;
    bool lockstep_ = false;
    SimGame game_;
    FileLoader load_;
    const std::vector<BasicParams>* params_ = nullptr;
    // The ring of pictures and what each is used for; viewed_ is the one view() shows (-1: the live state).
    std::array<std::unique_ptr<SimPicture>, 3> ring_;
    int viewed_ = -1;
    bool host_owns_ = false;  // a grant gave the game to the host; hold() gives it back
    DrawBlend blend_;         // Off: the blend snapshot of the last tick
    std::unique_ptr<Stash> stash_;

    // Shared with the sim thread, under mutex_ (held only to move a grant or a record, never while stepping or
    // drawing).
    std::mutex mutex_;
    std::condition_variable to_sim_, to_main_;
    std::deque<TickGrant> grants_;
    std::deque<TickRecord> records_;
    std::array<Slot, 3> slots_{Slot::Free, Slot::Free, Slot::Free};
    bool running_ = false;  // the sim thread is in a tick
    bool parked_ = true;    // the sim thread runs no tick (the main thread holds the game)
    bool stopped_ = false;  // a tick raised a game message: no tick until the next grant after its record was taken
    bool quit_ = false;
    double copy_ms_ = 0;
    std::uint32_t copies_ = 0;
#ifdef __EMSCRIPTEN__
    std::optional<pthread_t> thread_;  // the web build's SDL has no threads (SDL_PTHREADS is off there)
#else
    SDL_Thread* thread_ = nullptr;
#endif
};

}  // namespace antanta
