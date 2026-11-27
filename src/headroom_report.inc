static const char* headroom_layout=
    "frames lcd_off_frames double_speed_frames gb_run cpu_exec cpu_halt_step cpu_stopped_step cpu_irq "
    "cpu_halt_wake_noime cpu_halt_enter irq_bit[5] adv_calls adv_split_calls adv_hist[17] adv_cpu_cycles adv_dots "
    "adv_halted_calls adv_halted_dots adv_mode_dots[5] adv_mode_calls[5] apu_run_calls apu_samples display_run_calls "
    "div_apu_events tima_inc tima_overflow serial_master_edges serial_bits cycle_callback_calls dma_run_active dma_bytes "
    "hdma_run mbc_updates rom_bank_changes rd_region[16] wr_region[16] rd_io[256] wr_io[256] adv_large_cycles";
static auto headroom_words(const GB_headroom_counters_t& c) {
    std::array<uint64_t,sizeof(c)/sizeof(uint64_t)> values;
    static_assert(sizeof(values)==sizeof(c));
    std::memcpy(values.data(),&c,sizeof(c));return values;
}
std::string Machine::headroom_costs() const {
#if GBCLLR_HEADROOM_COUNTERS
    auto c=impl_->headroom_snapshot();auto values=headroom_words(c);
    std::ostringstream out;
    out<<"{\"enabled\":true,\"frames\":"<<c.frames<<",\"layout\":\""<<headroom_layout<<"\",\"values\":[";
    for(size_t i=0;i<values.size();++i){if(i)out<<',';out<<values[i];}
    out<<"],\"ring_samples\":"<<std::min<uint64_t>(c.frames/60,Impl::headroom_ring_capacity)<<'}';return out.str();
#else
    return "{\"enabled\":false}";
#endif
}
void Machine::headroom_report(const std::string& prefix) const {
#if GBCLLR_HEADROOM_COUNTERS
    if(prefix.empty())throw std::runtime_error("counter output prefix is empty");
    if(!impl_->headroom_pc_count.empty())impl_->headroom_charge_pc();
    std::ofstream out(prefix+".txt");
    if(!out)throw std::runtime_error("cannot write counter report");
    out<<"layout "<<headroom_layout<<'\n';
    auto write=[&](const char* tag,const GB_headroom_counters_t& c){out<<tag;for(auto v:headroom_words(c))out<<' '<<v;out<<'\n';};
    auto c=impl_->headroom_snapshot();write("total",c);
    auto samples=c.frames/60,n=std::min<uint64_t>(samples,Impl::headroom_ring_capacity);
    for(auto i=samples-n;i<samples;++i)write("ring",impl_->headroom_ring[i%Impl::headroom_ring_capacity]);
    if(!impl_->headroom_pc_count.empty()) {
        std::ofstream pc(prefix+".pc.txt");
        if(!pc)throw std::runtime_error("cannot write guest map");
        pc<<"# slot count units8 (slot<rom_size: ROM offset; else rom_size+pc) rom_size="<<impl_->rom.size()<<'\n';
        for(size_t i=0;i<impl_->headroom_pc_count.size();++i)
            if(impl_->headroom_pc_count[i]||impl_->headroom_pc_units[i])pc<<i<<' '<<impl_->headroom_pc_count[i]<<' '<<impl_->headroom_pc_units[i]<<'\n';
    }
#else
    (void)prefix;throw std::runtime_error("operation counters disabled in this build");
#endif
}
