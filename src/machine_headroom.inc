/* Machine-local diagnostic state; none of this enters a guest snapshot. */
std::vector<GB_headroom_counters_t> headroom_ring;
std::vector<uint32_t> headroom_pc_count;
std::vector<uint64_t> headroom_pc_units;
std::string headroom_prefix;
uint64_t headroom_pc_tick=0;
uint32_t headroom_pc_slot=0;
bool headroom_pc_valid=false;
static constexpr unsigned headroom_ring_capacity=1024;

GB_headroom_counters_t headroom_snapshot() const {
    auto c=gb.llr_headroom;
    c.cpu_exec=gb.llr_native_count+gb.llr_fallback_count;
    c.apu_run_calls=gb.llr_audio_costs.synthesis_calls;
    c.apu_samples=gb.llr_audio_costs.samples;
    c.display_run_calls=gb.llr_display_services;
    c.adv_cpu_cycles=c.adv_large_cycles;
    for(unsigned i=0;i<17;++i)c.adv_cpu_cycles+=i*c.adv_hist[i];
    c.adv_dots=0;
    for(auto n:c.adv_mode_dots)c.adv_dots+=n;
    return c;
}
void headroom_charge_pc() {
    auto now=gb.llr_tick_base+gb.cycles_since_run;
    if(headroom_pc_valid&&now>=headroom_pc_tick)
        headroom_pc_units[headroom_pc_slot]+=now-headroom_pc_tick;
    headroom_pc_tick=now;
}
static void executed_profiled(GB_gameboy_t* gb,uint16_t address,uint8_t opcode) {
    auto& m=self(gb);m.headroom_charge_pc();
    auto off=gb->llr_fetch_offset;
    m.headroom_pc_slot=off<gb->rom_size?off:gb->rom_size+address;
    ++m.headroom_pc_count[m.headroom_pc_slot];m.headroom_pc_valid=true;
    m.instruction_pc=address;executed(gb,address,opcode);
}
static void headroom_irq(GB_gameboy_t* gb) {
    auto& m=self(gb);m.headroom_charge_pc();
    m.headroom_pc_slot=gb->rom_size+0x10000;m.headroom_pc_valid=true;
}
void headroom_begin() {
#if GBCLLR_HEADROOM_COUNTERS
    if(auto p=std::getenv("HR_COUNTERS"))headroom_prefix=p;
    auto p=std::getenv("HR_GUEST_MAP");
    if(p&&*p&&std::strcmp(p,"0")) {
        auto n=rom.size()+0x10001;
        headroom_pc_count.resize(n);headroom_pc_units.resize(n);
        /* Reuse the existing execution observer. The default executor gets
         * no new per-instruction branch or callback. */
        GB_set_execution_callback(&gb,executed_profiled);
        gb.llr_headroom_irq=headroom_irq;
    }
#endif
}
void headroom_frame() {
#if GBCLLR_HEADROOM_COUNTERS
    auto& c=gb.llr_headroom;
    ++c.frames;
    if(!(gb.io_registers[GB_IO_LCDC]&0x80))++c.lcd_off_frames;
    if(gb.cgb_double_speed)++c.double_speed_frames;
    if(!headroom_pc_count.empty())headroom_charge_pc();
    if(c.frames%60==0) {
        if(headroom_ring.empty())headroom_ring.resize(headroom_ring_capacity);
        headroom_ring[(c.frames/60-1)%headroom_ring_capacity]=headroom_snapshot();
    }
#endif
}
