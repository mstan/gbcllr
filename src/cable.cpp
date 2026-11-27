#include <gbcllr/cable.hpp>
#include "continuation.hpp"
#include "gen2_native_mods.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gbcllr {
namespace {
constexpr uint64_t frame_ticks=140448;
constexpr uint64_t replay_window=32768,max_replay_ticks=frame_ticks*2,max_replay_points=4000000;
constexpr size_t max_replay_events=65536;
void put(std::vector<uint8_t>& out,uint64_t n) {
    for(unsigned i=0;i<8;++i)out.push_back(uint8_t(n>>(8*i)));
}
uint64_t get(std::span<const uint8_t>& in) {
    if(in.size()<8)throw std::runtime_error("truncated paired state");
    uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(in[i])<<(8*i);
    in=in.subspan(8);return n;
}
}
struct CablePair::Impl {
    enum class Phase {cycle,boundary,timers,edge,received};
    enum class EventKind:uint64_t {buttons,external_bit,internal_bit,external_rise};
    struct Event {uint64_t point,tick;EventKind kind;uint8_t value,output;};
    struct Endpoint {
        Impl* pair=nullptr;
        unsigned index=0;
        uint64_t tick=0;
        uint64_t clock_offset=0;
        uint64_t interval_start=0;
        uint64_t last_cycle=0;
        uint64_t idle_since=std::numeric_limits<uint64_t>::max();
        unsigned advance_depth=0;
        Phase phase=Phase::boundary;
        bool active=false,output=true,input=true;
        bool rising=false,wire_out=false;
        bool recording=false,replaying=false;
        uint64_t point=0,anchor_tick=0,target_point=0;
        size_t cursor=0;
        std::vector<uint8_t> anchor;
        std::vector<Event> events;
        std::unique_ptr<detail::Continuation> continuation;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
        bool serial_active=false; // Derived once at SC changes/completion/bind/restore.
        bool serial_internal=false;
        struct SerialPoint { uint64_t tick,point; };
        std::array<SerialPoint,32> serial_points{};
        unsigned serial_points_count=0;
        uint64_t serial_since=0;
        void serial_resumed() noexcept {serial_since=tick;serial_points_count=0;}
#endif
        uint64_t wire_tick(uint64_t local) noexcept {
            if(local>std::numeric_limits<uint64_t>::max()-clock_offset) {pair->failed=true;return std::numeric_limits<uint64_t>::max();}
            return local+clock_offset;
        }
        void record(EventKind kind,uint8_t value,uint64_t time=0,uint8_t old_output=0,
                    uint64_t site=std::numeric_limits<uint64_t>::max()) noexcept {
            if(!recording||replaying)return;
            if(events.size()>=max_replay_events){pair->failed=true;return;}
            try {events.push_back({site==std::numeric_limits<uint64_t>::max()?point:site,time,kind,value,old_output});}catch(...){pair->failed=true;}
        }
        void record_wire(EventKind kind,uint8_t value,uint64_t time,uint8_t old_output) noexcept {
            uint64_t site=point;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            // Journal the original bus observation, even when live execution
            // consumed the bit at a later serial/IRQ observation. Replaying
            // it there preserves the canonical continuation checkpoint.
            for(unsigned n=0;n<serial_points_count;++n)
                if(serial_points[n].tick>time){site=serial_points[n].point;break;}
#endif
            record(kind,value,time,old_output,site);
        }
        static void buttons(void* p,uint8_t value) {
            static_cast<Endpoint*>(p)->record(EventKind::buttons,value);
        }
        void injected() noexcept {
            while(cursor<events.size()&&events[cursor].point==point&&events[cursor].kind!=EventKind::internal_bit) {
                auto event=events[cursor++];
                if(event.kind==EventKind::buttons)pair->machines[index]->set_buttons(event.value);
                else if(event.kind==EventKind::external_rise) {
                    auto& machine=*pair->machines[index];
                    if(machine.serial_pin()!=bool(event.output))pair->failed=true;
                    auto bit=machine.serial_clock_edge(true);
                    if(!bit.accepted||bit.data_out!=bool(event.value))pair->failed=true;
                    wire_out=bit.data_out;
                }
                else {
                    auto bit=pair->machines[index]->serial_clock_edge(false,event.value!=0);
                    if(!bit.accepted||bit.data_out!=bool(event.output))pair->failed=true;
                }
            }
            if(cursor<events.size()&&events[cursor].point<point)pair->failed=true;
        }
        static bool pause(void* p,uint64_t tick,unsigned phase) noexcept {
            auto& e=*static_cast<Endpoint*>(p);
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            // A port state publication is not a CPU execution/replay point,
            // scheduler handoff or wire observation. Debug writes may be idle.
            if(phase==6){
                auto port=e.pair->machines[e.index]->serial_port();
                e.serial_active=port.active;e.serial_internal=port.internal_clock;return false;
            }
#endif
            auto local_tick=tick;tick=e.wire_tick(tick);
            bool boundary=phase==1;
            if((phase==4||phase==5)&&!e.active)return false; // Paused/debug writes already have synchronized ownership.
            if(!e.active) {e.pair->failed=true;return boundary;}
            if(e.pair->stopping)return boundary;
            bool cycles=e.pair->schedule==CableSchedule::cycle_continuations;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            const bool serial_active=e.serial_active;
#endif
            if(cycles) {
                // A native serial observation is at the existing bus point;
                // it does not add a new CPU/replay execution point.
                if(phase!=5)++e.point;
                e.pair->machines[e.index]->cooperative_state(true,boundary,local_tick);
#if GBCLLR_CABLE_ACTIVE_HORIZONS
                bool anchor_now=!serial_active;
#else
                constexpr bool anchor_now=true;
#endif
                if(anchor_now&&!e.replaying&&boundary&&e.anchor_tick<e.pair->anchor_min&&tick>=e.pair->anchor_min) {
                    try {e.anchor=e.pair->machines[e.index]->snapshot();e.anchor_tick=tick;e.point=0;e.events.clear();}
                    catch(...) {e.pair->failed=true;e.continuation->yield();return boundary;}
                }
            }
            bool expose=true;
#if GBCLLR_CABLE_BUS_BATCHES
            if(phase==3) {
                if(!e.advance_depth++)e.interval_start=tick;
                expose=false;
            }
            if(phase==2)expose=false;
            if(phase==0) {
                if(!e.advance_depth) {e.pair->failed=true;return false;}
                if(--e.advance_depth)expose=false;
            }
            // The completed device interval already yielded before any next
            // CPU bus access. Only expose instruction boundaries when looking
            // for the requested checkpoint, or while draining an instruction.
            if(boundary&&!cycles&&tick<e.pair->stop_tick)expose=false;
#endif
            if(phase==0&&!e.advance_depth) {
#if !GBCLLR_CABLE_BUS_BATCHES
                e.interval_start=e.last_cycle;
#endif
                e.last_cycle=tick;
            }
            if(e.replaying) {
                e.injected();
                if(e.point>e.target_point||e.point>max_replay_points)e.pair->failed=true;
                if(e.point==e.target_point||e.pair->failed) {
                    if(phase!=0&&phase!=1&&phase!=4)e.pair->failed=true;
                    e.tick=tick;e.phase=boundary?Phase::boundary:Phase::cycle;
                    e.continuation->yield();
                    return e.pair->stopping&&boundary;
                }
                return false;
            }
#if GBCLLR_CABLE_EVENT_HORIZONS
            // SC is still inactive at the pre-write observation. Its old SO
            // latch remains valid for every earlier peer edge. An inactive
            // console cannot consume serial bits or receive a serial IRQ, so
            // none of its intervening CPU/device work needs scheduling.
            if(cycles) {
                if(
#if GBCLLR_CABLE_ACTIVE_HORIZONS
                   serial_active
#else
                   e.pair->machines[e.index]->serial_port().active
#endif
                )
                    e.idle_since=std::numeric_limits<uint64_t>::max();
                else {
                    if(e.idle_since==std::numeric_limits<uint64_t>::max())e.idle_since=tick;
                    if(phase!=4&&tick<e.pair->stop_tick) {
                        if(expose)++e.pair->execution.idle_yields_elided;
                        expose=false;
                    }
                }
            }
#endif
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            if(cycles&&serial_active) {
                if(phase==0&&!e.advance_depth&&e.serial_points_count<e.serial_points.size())
                    e.serial_points[e.serial_points_count++]={tick,e.point};
                // CPU instruction/IRQ observations, explicit port accesses,
                // actual pin edges and the requested horizon remain public.
                // Only their intervening ordinary bus intervals disappear.
                if(phase==0&&!e.advance_depth&&tick<e.pair->stop_tick&&
                   e.serial_points_count<e.serial_points.size())expose=false;
            }
#endif
#if GBCLLR_CABLE_BYTE_OPERATIONS
            if(cycles&&serial_active&&e.pair->native_exchange_allowed&&tick<e.pair->stop_tick) {
                const auto& source=e.pair->endpoints[e.index^1];
                // An internal clock can advance to its next real wire/register
                // event: the peer cannot supply its serial IRQ. An external
                // CPU advances only to that already-known event, never beyond
                // the first device interval after it. This retains the original
                // IRQ sample ordering at an equal-tick instruction boundary.
                // An inactive peer has likewise proved no clock before its
                // paused SC activation/checkpoint, including inter-byte waits.
                const bool refresh=boundary&&e.anchor_tick<e.pair->anchor_min&&tick>=e.pair->anchor_min;
                if(!refresh&&(boundary||phase==5)&&(e.serial_internal||tick<=source.tick)) {
                    if(expose)++e.pair->execution.exchange_handoffs_elided;
                    expose=false;
                }
                if(!e.serial_internal&&phase==0&&!e.advance_depth&&tick>source.tick)
                    expose=true;
            }
#endif
            if(!expose)return false;
            e.tick=tick;e.phase=phase==2?Phase::timers:boundary?Phase::boundary:Phase::cycle;
            if(e.pair->schedule==CableSchedule::cycle_continuations)
                e.pair->machines[e.index]->cooperative_state(true,boundary,local_tick);
            e.continuation->yield();
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            e.serial_resumed();
            // Pending wire bits must be consumed before making the anchor.
            // No CPU operation has resumed at this instruction boundary.
            if(cycles&&!e.replaying&&!e.pair->stopping&&boundary&&
               e.anchor_tick<e.pair->anchor_min&&tick>=e.pair->anchor_min) {
                try {e.anchor=e.pair->machines[e.index]->snapshot();e.anchor_tick=tick;e.point=0;e.events.clear();}
                catch(...) {e.pair->failed=true;}
            }
#endif
            return e.pair->stopping&&boundary;
        }
        static bool exchange(void* p,uint64_t tick,bool output) noexcept {
            auto& e=*static_cast<Endpoint*>(p);
            tick=e.wire_tick(tick);
            if(e.pair->stopping)return true;
            if(e.replaying) {
                if(e.cursor==e.events.size()){e.pair->failed=true;return true;}
                auto event=e.events[e.cursor++];
                if(event.kind!=EventKind::internal_bit||event.point!=e.point||event.tick!=tick||event.output!=uint8_t(output))
                    e.pair->failed=true;
                return event.value!=0;
            }
            // An offline SC/DIV write may clock a wire at a synchronized
            // checkpoint. Both machines are already paused at this tick, so
            // sample the peer's old bit before shifting its external receiver.
            // Never enter a CPU continuation from the caller's stack.
            if(!e.active) {
                auto& pair=*e.pair;auto& peer=*pair.machines[e.index^1];
                auto port=peer.serial_port();
                if(pair.failed||e.wire_tick(pair.machines[e.index]->stats().cycles)!=tick||pair.endpoints[e.index^1].wire_tick(peer.stats().cycles)!=tick||
                   pair.endpoints[e.index^1].active||(port.active&&port.internal_clock)) {
                    pair.failed=true;return true;
                }
                bool input=pair.endpoints[e.index^1].wire_out;
                if(port.active)peer.serial_clock_edge(false,output);
                return input;
            }
            e.tick=tick;e.phase=Phase::edge;e.output=output;e.rising=false;
            e.continuation->yield();
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            e.serial_resumed();
#endif
            e.record(EventKind::internal_bit,uint8_t(e.input),tick,uint8_t(output));
            return e.input;
        }
        static void rise(void* p,uint64_t tick,bool output) noexcept {
            auto& e=*static_cast<Endpoint*>(p);tick=e.wire_tick(tick);
            if(e.pair->stopping)return;
            if(e.replaying){e.wire_out=output;return;}
            auto& pair=*e.pair;
            if(!e.active) {
                auto& peer=*pair.machines[e.index^1];auto port=peer.serial_port();
                if(pair.failed||e.wire_tick(pair.machines[e.index]->stats().cycles)!=tick||
                   pair.endpoints[e.index^1].wire_tick(peer.stats().cycles)!=tick||pair.endpoints[e.index^1].active) {
                    pair.failed=true;return;
                }
                e.wire_out=output;
                if(port.active&&!port.internal_clock)pair.endpoints[e.index^1].wire_out=peer.serial_clock_edge(true).data_out;
                return;
            }
            e.tick=tick;e.phase=Phase::edge;e.output=output;e.rising=true;
            e.continuation->yield();e.rising=false;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            e.serial_resumed();
#endif
        }
    };
    std::array<std::vector<uint8_t>,2> roms;
    std::array<Options,2> options;
    std::array<std::unique_ptr<Machine>,2> machines;
    std::array<Endpoint,2> endpoints;
    uint64_t frame_deadline=0,stop_tick=0,anchor_min=0;
    bool stopping=false,failed=false,clocks_bound=false;
    CableSchedule schedule;
    bool native_exchange_allowed=false;
    CableExecutionStats execution{};
    Impl(std::span<const uint8_t> left,Options lo,std::span<const uint8_t> right,Options ro,CableSchedule mode):
        roms{std::vector<uint8_t>(left.begin(),left.end()),std::vector<uint8_t>(right.begin(),right.end())},
        options{std::move(lo),std::move(ro)},schedule(mode) {
#if GBCLLR_CABLE_BYTE_OPERATIONS
        native_exchange_allowed=mode==CableSchedule::cycle_continuations&&
            options[0].mods.empty()&&options[1].mods.empty();
#endif
        if(!options[0].mods.empty()||!options[1].mods.empty()) {
            if(!Gen2NativeMods::linked_pair(sha256(left),sha256(right)))
                throw std::runtime_error("native linked views require the qualified Gold/Silver cartridge pair");
            if(mode!=CableSchedule::cycle_continuations)
                throw std::runtime_error("Gen2 native views require the qualified cycle cable profile");
            for(const auto& o:options)if(!Gen2NativeMods::linked_profile(o.mods,o.model==Model::cgb))
                throw std::runtime_error("native feature sets require a qualified common cable profile");
        }
        for(unsigned i=0;i<2;++i) {
            options[i].reference_picture=true;
            machines[i]=std::make_unique<Machine>(roms[i],options[i]);
            auto& e=endpoints[i];e.pair=this;e.index=i;
            e.continuation=std::make_unique<detail::Continuation>([this,i] {
                auto& current=endpoints[i];
                if(current.replaying&&current.target_point==0) {
                    current.tick=current.anchor_tick;current.phase=Phase::boundary;
                    machines[i]->cooperative_state(true,true,current.tick-current.clock_offset);
                    current.injected();current.continuation->yield();
                    machines[i]->cooperative_state(false,true,0);
                }
                try {machines[i]->run_until(std::numeric_limits<uint64_t>::max());}
                catch(...) {current.active=false;machines[i]->cooperative_state(false,true,0);throw;}
                current.active=false;
                machines[i]->cooperative_state(false,true,0);
            });
            bind(i);
        }
    }
    void bind(unsigned i) {
        auto& e=endpoints[i];e.tick=e.wire_tick(machines[i]->stats().cycles);e.phase=Phase::boundary;
        e.interval_start=e.tick;e.advance_depth=0;
        e.last_cycle=e.tick;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
        e.serial_resumed();
#endif
        const auto port=machines[i]->serial_port();const bool serial_active=port.active;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
        e.serial_active=serial_active;e.serial_internal=port.internal_clock;
#endif
        e.idle_since=serial_active?std::numeric_limits<uint64_t>::max():e.tick;
        e.wire_out=machines[i]->serial_pin();e.rising=false;
        machines[i]->execution_hook(&e,Endpoint::pause);
        machines[i]->link_callbacks({&e,Endpoint::exchange,nullptr,Endpoint::rise});
        machines[i]->input_hook(&e,Endpoint::buttons);
    }
    ~Impl() {drain();}
    void drain() noexcept {
        stopping=true;
        for(auto& e:endpoints)while(e.active) {
            try {e.continuation->resume();}catch(...) {e.active=false;}
        }
        stopping=false;
    }
    void exchange(uint64_t tick) {
        // Both CPUs are suspended before any following bus operation. The
        // peer may be in its timer callback too, so preserve both output bits
        // before shifting an external receiver.
        std::array<SerialPort,2> ports{machines[0]->serial_port(),machines[1]->serial_port()};
        std::array<bool,2> edge{},out{};
        for(unsigned i=0;i<2;++i) {
            auto& e=endpoints[i];
            bool aligned=e.tick==tick;
#if GBCLLR_CABLE_EVENT_HORIZONS
            aligned|=schedule==CableSchedule::cycle_continuations&&!ports[i].active&&
                e.idle_since<=tick&&tick<e.tick;
#endif
#if GBCLLR_CABLE_BUS_BATCHES
            // The other devices do not consume SB/SC or the serial IRQ. While
            // suspended at the end of one outer advance, the port still has
            // its value from interval_start. Apply all intervening wire edges
            // before allowing any following CPU bus access or IRQ sampling.
            aligned|=e.phase==Phase::cycle&&e.interval_start<=tick&&tick<e.tick;
#endif
            aligned|=schedule==CableSchedule::cycle_continuations&&e.phase==Phase::cycle&&
                e.interval_start<=tick&&tick<e.tick;
            // A peer can be suspended at a later internal half-edge inside
            // this same bus-free advance. No CPU access has followed
            // last_cycle, and its pending edge has not changed wire_out.
            // Sample that old latched SO without advancing its own clock.
            aligned|=e.phase==Phase::edge&&ports[i].internal_clock&&
                std::max(e.interval_start,e.last_cycle)<=tick&&tick<e.tick;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
            // Between these points the CPU cannot observe a serial register
            // or sample a serial IRQ. Its old output latch remains valid.
            aligned|=schedule==CableSchedule::cycle_continuations&&
                e.serial_since<=tick&&tick<e.tick;
#endif
            if(!aligned)throw std::runtime_error("cable edge "+std::to_string(tick)+" is not aligned with peer T cycle "+std::to_string(e.tick)+
                " phase="+std::to_string(unsigned(e.phase))+" interval="+std::to_string(e.interval_start)+" last="+std::to_string(e.last_cycle)+
                " depth="+std::to_string(e.advance_depth)+" rising="+std::to_string(e.rising));
            edge[i]=e.phase==Phase::edge&&e.tick==tick;
            out[i]=endpoints[i].wire_out;
        }
        // Sample old SO before any same-tick rising edge updates a latch.
        // An internal source selects its own clock, never its peer's edge.
        for(unsigned i=0;i<2;++i)if(edge[i]&&!endpoints[i].rising) {
            unsigned peer=i^1;
            if(ports[peer].active&&!ports[peer].internal_clock) {
                ++execution.wire_peer_bits;
                endpoints[peer].record_wire(EventKind::external_bit,uint8_t(out[i]),tick,uint8_t(out[peer]));
                machines[peer]->serial_clock_edge(false,out[i]);
            }
            endpoints[i].input=out[peer];endpoints[i].phase=Phase::received;
        }
        for(unsigned i=0;i<2;++i)if(edge[i]&&endpoints[i].rising) {
            unsigned peer=i^1;endpoints[i].wire_out=endpoints[i].output;
#if GBCLLR_CABLE_BYTE_OPERATIONS
            if(!ports[i].shifted_bits)++execution.native_exchanges;
#endif
            if(ports[peer].active&&!ports[peer].internal_clock) {
                auto bit=machines[peer]->serial_clock_edge(true);
                endpoints[peer].record_wire(EventKind::external_rise,uint8_t(bit.data_out),tick,uint8_t(out[peer]));
                endpoints[peer].wire_out=bit.data_out;
            }
            endpoints[i].phase=Phase::received;
        }
    }
    void run(uint64_t deadline) {
        if(failed)throw std::runtime_error("restore a paired checkpoint after cable execution failure");
        bool active=endpoints[0].active||endpoints[1].active;
        if(!active&&schedule==CableSchedule::common_instruction&&machines[0]->stats().cycles!=machines[1]->stats().cycles)
            throw std::runtime_error("consoles were independently advanced outside their cable pair");
        if(!active&&schedule==CableSchedule::cycle_continuations&&clocks_bound)
            for(unsigned i=0;i<2;++i)if(endpoints[i].wire_tick(machines[i]->stats().cycles)!=endpoints[i].tick)
                throw std::runtime_error("console clock changed after cable attachment");
        if(!active&&schedule==CableSchedule::cycle_continuations&&!clocks_bound) {
            // Independent consoles have independent clock origins. Attach
            // their paused states at one wire instant without advancing either
            // guest or changing its device/RTC counters.
            auto common=std::max(machines[0]->stats().cycles,machines[1]->stats().cycles);
            for(unsigned i=0;i<2;++i) {endpoints[i].clock_offset=common-machines[i]->stats().cycles;bind(i);}
            clocks_bound=true;
        }
        if(std::min(endpoints[0].wire_tick(machines[0]->stats().cycles),endpoints[1].wire_tick(machines[1]->stats().cycles))>=deadline)return;
        stop_tick=deadline;
        anchor_min=deadline>replay_window?deadline-replay_window:0;
        stopping=false;
        if(!active)for(unsigned i=0;i<2;++i) {
            auto& e=endpoints[i];e.tick=e.wire_tick(machines[i]->stats().cycles);
            if(schedule==CableSchedule::cycle_continuations) {
                e.anchor=machines[i]->snapshot();e.anchor_tick=e.tick;e.point=0;e.events.clear();
                e.events.reserve(max_replay_events);e.recording=true;
            }
            e.active=true;
        }
        try {
            for(;;) {
                if(failed)throw std::runtime_error("invalid cable CPU bus interval");
                auto& a=endpoints[0];auto& b=endpoints[1];
                auto complete_interval=[](const Endpoint& e){return e.phase==Phase::cycle||e.phase==Phase::boundary;};
                if(schedule==CableSchedule::cycle_continuations&&complete_interval(a)&&complete_interval(b)&&
                   a.tick>=deadline&&b.tick>=deadline)break;
                if(a.phase==Phase::boundary&&b.phase==Phase::boundary&&a.tick==b.tick&&a.tick>=deadline)break;
                // Never spin indefinitely trying to serialize a suspended CPU
                // stack. A future continuation format must cover programs
                // with permanently disjoint instruction-boundary phases.
                if(std::min(a.tick,b.tick)>deadline && std::min(a.tick,b.tick)-deadline>frame_ticks)
                    throw std::runtime_error("no common cable instruction boundary within one extra frame");
                auto edge_tick=std::numeric_limits<uint64_t>::max();
                for(auto& e:endpoints)if(e.phase==Phase::edge)edge_tick=std::min(edge_tick,e.tick);
                auto ready=[&](const Endpoint& e) {return e.tick>edge_tick ||
                    (e.tick==edge_tick && (e.phase==Phase::timers || e.phase==Phase::edge));};
                if(edge_tick!=std::numeric_limits<uint64_t>::max()&&ready(a)&&ready(b)) {
                    exchange(edge_tick);continue;
                }
                unsigned next=b.tick<a.tick?1:0;
                if(a.tick==b.tick && unsigned(b.phase)<unsigned(a.phase))next=1;
                // An unresolved edge waits for the other endpoint to reach it.
                if(endpoints[next].phase==Phase::edge)next^=1;
                ++execution.continuation_yields;
                endpoints[next].continuation->resume();
            }
            if(schedule==CableSchedule::common_instruction)drain();
        } catch(...) {failed=true;drain();throw;}
    }
};
CablePair::CablePair(std::span<const uint8_t> l,Options lo,std::span<const uint8_t> r,Options ro,CableSchedule mode):
    impl_(std::make_unique<Impl>(l,std::move(lo),r,std::move(ro),mode)) {}
CablePair::~CablePair()=default;
Machine& CablePair::machine(unsigned i) {return *impl_->machines.at(i);}
const Machine& CablePair::machine(unsigned i) const {return *impl_->machines.at(i);}
uint64_t CablePair::ticks() const {return std::max(impl_->endpoints[0].wire_tick(machine(0).stats().cycles),impl_->endpoints[1].wire_tick(machine(1).stats().cycles));}
const char* CablePair::continuation_backend() const noexcept {return impl_->endpoints[0].continuation->backend();}
CableExecutionStats CablePair::execution_stats() const noexcept {return impl_->execution;}
void CablePair::run_until(uint64_t deadline) {
    // Keep replay history bounded even when a caller requests a long run.
    // Cycle mode always exposes instruction boundaries, so these internal
    // stops do not change IRQ sampling or CPU ordering.
    if(impl_->schedule==CableSchedule::cycle_continuations) {
        for(;;) {
            auto begin=impl_->clocks_bound?std::min(impl_->endpoints[0].wire_tick(machine(0).stats().cycles),impl_->endpoints[1].wire_tick(machine(1).stats().cycles)):ticks();
            if(deadline<=begin||deadline-begin<=frame_ticks)break;
            impl_->run(begin+frame_ticks);
        }
    }
    impl_->run(deadline);
}
void CablePair::run_frame(uint8_t left,uint8_t right) {
    machine(0).set_buttons(left);machine(1).set_buttons(right);
    if(impl_->frame_deadline<ticks())impl_->frame_deadline=(ticks()/frame_ticks)*frame_ticks;
    impl_->frame_deadline+=frame_ticks;run_until(impl_->frame_deadline);
    machine(0).headroom_frame_end();machine(1).headroom_frame_end();
}
std::vector<uint8_t> CablePair::snapshot() const {
    if(impl_->failed)throw std::runtime_error("cannot checkpoint failed cable execution");
    // Emit the original machine codecs straight into their containing archive.
    // Prefix indices survive growth; the native machine SHA covers only its
    // own appended bytes. No retained state or dirty-memory cache is involved.
    auto append_machines=[&](std::vector<uint8_t>& out,bool continuation) {
        put(out,impl_->frame_deadline);const auto prefix=out.size();put(out,0);put(out,0);
        std::array<std::pair<size_t,size_t>,2> sections{};
        for(unsigned i=0;i<2;++i) {
            const auto begin=out.size();machine(i).append_state(out,continuation);
            const auto size=out.size()-begin;
            if(size>2*1024*1024)throw std::runtime_error("paired machine snapshot exceeds codec bound");
            sections[i]={begin,size};
            for(unsigned byte=0;byte<8;++byte)out[prefix+i*8+byte]=uint8_t(uint64_t(size)>>(8*byte));
        }
        return sections;
    };
    if(impl_->schedule==CableSchedule::cycle_continuations) {
        std::vector<uint8_t> out{'G','B','P','A','I','R',4,0};
        const auto sections=append_machines(out,true);
        put(out,impl_->stop_tick);
        put(out,impl_->clocks_bound);
        for(unsigned i=0;i<2;++i) {
            const auto& e=impl_->endpoints[i];
            if(e.active&&(e.phase!=Impl::Phase::cycle&&e.phase!=Impl::Phase::boundary))
                throw std::runtime_error("cable checkpoint has an unresolved wire edge");
            const auto anchor_size=e.active?e.anchor.size():sections[i].second;
            auto time=e.active?e.tick:e.clock_offset+machine(i).stats().cycles;
            auto start=e.active?e.anchor_tick:time;
            if(time<start||time-start>max_replay_ticks||e.point>max_replay_points)
                throw std::runtime_error("cable continuation exceeds its replay bound");
            put(out,e.active);put(out,e.active?e.point:0);put(out,time);
            put(out,uint64_t(e.active?e.phase:Impl::Phase::boundary));
            put(out,e.active?e.interval_start:time);put(out,e.active?e.last_cycle:time);
            put(out,start);put(out,anchor_size);put(out,e.active?e.events.size():0);
            put(out,e.clock_offset);
            if(e.active)out.insert(out.end(),e.anchor.begin(),e.anchor.end());
            else {
                // Resize first, then recover both pointers by index. Inserting
                // a vector's own iterator range would invalidate the source.
                const auto begin=out.size();out.resize(begin+anchor_size);
                std::memcpy(out.data()+begin,out.data()+sections[i].first,anchor_size);
            }
            if(e.active)for(const auto& event:e.events) {
                put(out,event.point);put(out,uint64_t(event.kind));put(out,event.tick);put(out,event.value);put(out,event.output);
            }
        }
        auto hash=sha256(out);out.insert(out.end(),hash.begin(),hash.end());return out;
    }
    if(impl_->endpoints[0].active||impl_->endpoints[1].active)
        throw std::runtime_error("cycle-continuation checkpoint codec is not implemented");
    if(machine(0).stats().cycles!=machine(1).stats().cycles)
        throw std::runtime_error("cannot checkpoint unsynchronized consoles");
    std::vector<uint8_t> out{'G','B','P','A','I','R',1,0};
    append_machines(out,false);
    auto hash=sha256(out);out.insert(out.end(),hash.begin(),hash.end());return out;
}
bool CablePair::restore(std::span<const uint8_t> data,std::string& error) {
    try {
        const std::array<uint8_t,8> cycle_magic{'G','B','P','A','I','R',4,0};
        if(data.size()>=8&&std::equal(cycle_magic.begin(),cycle_magic.end(),data.begin())) {
            if(impl_->schedule!=CableSchedule::cycle_continuations)throw std::runtime_error("cable schedule mismatch");
            if(data.size()<104||data.size()>16*1024*1024||sha256(data.first(data.size()-64))!=
               std::string(reinterpret_cast<const char*>(data.data()+data.size()-64),64))
                throw std::runtime_error("paired continuation checksum/size mismatch");
            auto payload=data.subspan(8,data.size()-8-64);auto frame=get(payload),left=get(payload),right=get(payload);
            if(left>2*1024*1024||right>2*1024*1024||left>payload.size()||right>payload.size()-left)
                throw std::runtime_error("paired continuation lengths mismatch");
            std::array<std::vector<uint8_t>,2> expected;
            expected[0]={payload.begin(),payload.begin()+size_t(left)};
            expected[1]={payload.begin()+size_t(left),payload.begin()+size_t(left+right)};
            payload=payload.subspan(size_t(left+right));auto stop=get(payload);
            auto bound=get(payload);if(bound>1)throw std::runtime_error("invalid cable clock mapping");
            auto staged=std::make_unique<Impl>(impl_->roms[0],impl_->options[0],impl_->roms[1],impl_->options[1],impl_->schedule);
            staged->frame_deadline=frame;staged->stop_tick=stop;staged->clocks_bound=bound;
            for(unsigned i=0;i<2;++i) {
                auto live=get(payload),point=get(payload),tick=get(payload),phase=get(payload);
                auto interval=get(payload),last=get(payload),start=get(payload),size=get(payload),count=get(payload);
                auto offset=get(payload);
                if(live>1||phase>uint64_t(Impl::Phase::boundary)||point>max_replay_points||tick<start||
                   tick-start>max_replay_ticks||interval>tick||last!=tick||size>2*1024*1024||size>payload.size()||count>max_replay_events||
                   offset>start||(!bound&&(live||offset)))
                    throw std::runtime_error("invalid cable continuation descriptor");
                std::vector<uint8_t> anchor(payload.begin(),payload.begin()+size_t(size));payload=payload.subspan(size_t(size));
                std::vector<Impl::Event> events;events.reserve(size_t(count));uint64_t previous=0;
                for(uint64_t n=0;n<count;++n) {
                    auto site=get(payload),kind=get(payload),time=get(payload),value=get(payload),output=get(payload);
                    if(site<previous||site>point||kind>3||value>(kind==0?255u:1u)||output>1||
                       (kind!=0&&(time<start||time>tick)))throw std::runtime_error("invalid cable continuation event");
                    previous=site;events.push_back({site,time,Impl::EventKind(kind),uint8_t(value),uint8_t(output)});
                }
                if(!staged->machines[i]->restore(anchor,error))throw std::runtime_error(error);
                if(staged->machines[i]->stats().cycles!=start-offset)throw std::runtime_error("cable anchor time mismatch");
                staged->endpoints[i].clock_offset=offset;
                staged->bind(i);auto& e=staged->endpoints[i];e.anchor=std::move(anchor);e.anchor_tick=start;
                e.events=std::move(events);e.events.reserve(max_replay_events);e.recording=true;
                if(live) {
                    e.active=true;e.replaying=true;e.target_point=point;
                    if(!point) {e.interval_start=interval;e.last_cycle=last;}
                    e.continuation->resume();
                    if(staged->failed||!e.active||e.point!=point||e.cursor!=e.events.size()||e.tick!=tick||
                       uint64_t(e.phase)!=phase||e.interval_start!=interval||e.last_cycle!=last||e.advance_depth)
                        throw std::runtime_error("cable continuation replay diverged");
                    e.replaying=false;staged->machines[i]->take_audio();
                } else if(point||count||phase!=uint64_t(Impl::Phase::boundary)||tick!=start||e.anchor!=expected[i])
                    throw std::runtime_error("invalid idle cable continuation");
                if(staged->machines[i]->continuation_view()!=expected[i])throw std::runtime_error("cable continuation state differs");
            }
            if(!payload.empty())throw std::runtime_error("unexpected cable continuation data");
            // Host counters are not guest rollback state. The dormant horizon
            // itself is derived again from the restored checkpoint; accepting
            // only future edges is conservative even when replay began idle.
            for(auto& e:staged->endpoints) {
                const auto port=staged->machines[e.index]->serial_port();const bool serial_active=port.active;
                e.idle_since=serial_active?std::numeric_limits<uint64_t>::max():e.tick;
#if GBCLLR_CABLE_ACTIVE_HORIZONS
                e.serial_active=serial_active;e.serial_internal=port.internal_clock;
#endif
#if GBCLLR_CABLE_ACTIVE_HORIZONS
                e.serial_resumed();
                if(e.phase==Impl::Phase::cycle&&e.interval_start<e.tick) {
                    e.serial_since=e.interval_start;
                    e.serial_points[e.serial_points_count++]={e.tick,e.point};
                }
#endif
            }
            staged->execution=impl_->execution;
            impl_.swap(staged);error.clear();return true;
        }
        if(impl_->endpoints[0].active||impl_->endpoints[1].active)
            throw std::runtime_error("cycle-continuation checkpoint codec is not implemented");
        const std::array<uint8_t,8> magic{'G','B','P','A','I','R',1,0};
        if(data.size()<96 || !std::equal(magic.begin(),magic.end(),data.begin()) ||
           sha256(data.first(data.size()-64))!=std::string(reinterpret_cast<const char*>(data.data()+data.size()-64),64))
            throw std::runtime_error("paired state identity/checksum mismatch");
        auto payload=data.subspan(8,data.size()-8-64);
        auto frame=get(payload),left=get(payload),right=get(payload);
        if(left>payload.size()||right!=payload.size()-left)throw std::runtime_error("paired state lengths mismatch");
        std::array<std::unique_ptr<Machine>,2> staged;
        for(unsigned i=0;i<2;++i) {
            staged[i]=std::make_unique<Machine>(impl_->roms[i],impl_->options[i]);
            auto part=i?payload.subspan(size_t(left)):payload.first(size_t(left));
            if(!staged[i]->restore(part,error))throw std::runtime_error(error);
        }
        if(staged[0]->stats().cycles!=staged[1]->stats().cycles)throw std::runtime_error("paired state time mismatch");
        impl_->machines.swap(staged);impl_->frame_deadline=frame;impl_->failed=false;
        impl_->clocks_bound=false;for(auto& e:impl_->endpoints)e.clock_offset=0;
        impl_->bind(0);impl_->bind(1);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
