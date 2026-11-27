#pragma once
#include <gbcllr/machine.hpp>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace gbcllr {
// The existing state encoder writes straight into retained state. Preserve an
// old page before its first changed write, without a scratch archive/diff pass.
// External files continue through the validated sealed codec.
class RewindHistory {
public:
    enum class Storage { automatic, reverse_pages, checkpoints };
private:
    static constexpr size_t page=256;
    struct Entry {
        uint32_t tick=0;bool valid=false;size_t old_size=0;
        std::vector<uint32_t> offsets;
        std::vector<uint8_t> before;
        Machine::RewindState checkpoint;
    };
    std::vector<Entry> entries_;
    Machine::RewindState current_,view_;
    std::vector<uint8_t> touched_;
    uint32_t latest_=0;
    bool current_valid_=false,direct_=false;
    Storage storage_;
    size_t checkpoint_size_=0;
    std::shared_ptr<const char> owner_;
    struct Capture {RewindHistory* history;Entry* entry;size_t position=0;};
    void preserve_page(Entry& entry,size_t at) {
        if(at>=entry.old_size||touched_[at/page])return;
        auto n=std::min(page,entry.old_size-at);
        auto& state=current_.bytes_;
        // If allocation fails, no corresponding live bytes have changed yet.
        entry.before.insert(entry.before.end(),state.data()+at,state.data()+at+n);
        entry.offsets.push_back(uint32_t(at));touched_[at/page]=1;
    }
    void write(Capture& capture,const uint8_t* input,size_t count) {
        auto& state=current_.bytes_;auto& entry=*capture.entry;
        if(count>state.max_size()-capture.position)throw std::length_error("rewind state too large");
        if(state.size()<capture.position+count)state.resize(capture.position+count);
        while(count) {
            size_t at=capture.position,n=std::min(count,page-at%page);
            if(std::memcmp(state.data()+at,input,n)) {
                if(at<entry.old_size)preserve_page(entry,at-at%page);
                std::memcpy(state.data()+at,input,n);
            }
            capture.position+=n;input+=n;count-=n;
        }
    }
    static void chunk(void* context,const uint8_t* input,size_t count) {
        auto& capture=*static_cast<Capture*>(context);capture.history->write(capture,input,count);
    }
    static void undo(std::vector<uint8_t>& state,const Entry& entry) {
        state.resize(entry.old_size);size_t from=0;
        for(auto at:entry.offsets) {
            auto n=std::min(page,entry.old_size-at);
            std::memcpy(state.data()+at,entry.before.data()+from,n);from+=n;
        }
    }
public:
    explicit RewindHistory(size_t depth,Storage storage=Storage::automatic):entries_(depth),storage_(storage) {
        if(depth<2||depth>300)throw std::invalid_argument("rewind history depth must be 2..300");
    }
    void clear(){for(auto& e:entries_)e.valid=false;current_valid_=false;owner_.reset();}
    void store(const Machine& m,uint32_t tick){
        if(current_valid_&&(tick==0||tick!=latest_+1||owner_!=m.checkpoint_owner_))clear();
        if(!current_valid_)direct_=storage_==Storage::checkpoints||
            (storage_==Storage::automatic&&m.direct_checkpoint_history());
        if(direct_) {
            auto& e=entries_[tick%entries_.size()];e.valid=false;
            // Each retained allocation is reused after wrap. One direct typed
            // checkpoint replaces page comparisons and reverse-patch assembly.
            e.checkpoint.bytes_.reserve(checkpoint_size_);
            m.checkpoint(e.checkpoint);checkpoint_size_=e.checkpoint.bytes_.size();
            if(!current_valid_) {
                // Provision and touch the selected bounded ring once. Steady
                // capture should neither allocate nor fault in a new archive.
                for(auto& slot:entries_)if(&slot!=&e)
                    slot.checkpoint.bytes_.resize(checkpoint_size_);
            }
            owner_=m.checkpoint_owner_;latest_=tick;current_valid_=true;e.tick=tick;e.valid=true;
            return;
        }
        if(!current_valid_)current_.bytes_.clear();
        auto& e=entries_[tick%entries_.size()];e.valid=false;
        e.offsets.clear();e.before.clear();e.old_size=current_.bytes_.size();
        touched_.assign((e.old_size+page-1)/page,0);
        Capture capture{this,&e};
        try {
            m.checkpoint_chunks(&capture,chunk);
            // Preserve truncated tails too, so a variable-size state reverses
            // through the same page representation as an ordinary write.
            if(capture.position<e.old_size)
                for(size_t at=capture.position-capture.position%page;at<e.old_size;at+=page)
                    preserve_page(e,at);
            current_.bytes_.resize(capture.position);
        } catch(...) {
            undo(current_.bytes_,e);throw;
        }
        current_.owner_=m.checkpoint_owner_;owner_=m.checkpoint_owner_;latest_=tick;current_valid_=true;e.tick=tick;e.valid=true;
    }
    const Machine::RewindState* peek(uint32_t tick){
        auto& target=entries_[tick%entries_.size()];
        if(!current_valid_||!target.valid||target.tick!=tick||tick>latest_)return nullptr;
        if(direct_)return &target.checkpoint;
        if(tick==latest_)return &current_;
        view_=current_;
        for(uint32_t t=latest_;t>tick;--t){
            auto& e=entries_[t%entries_.size()];if(!e.valid||e.tick!=t)return nullptr;
            undo(view_.bytes_,e);
        }
        return &view_;
    }
    void drop_after(uint32_t tick){
        if(current_valid_&&tick<latest_){
            auto* state=peek(tick);if(!state)throw std::runtime_error("rewind branch target unavailable");
            if(!direct_)std::swap(current_,view_);latest_=tick;
        }
        for(auto& e:entries_)if(e.valid&&e.tick>tick)e.valid=false;
    }
};
}
