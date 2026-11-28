#pragma once
#include <gbcllr/machine.hpp>

inline std::vector<uint8_t> native_contract_rom() {
    return gbcllr::read_file(GBCLLR_NATIVE_CONTRACT_ROM);
}

inline void native_contract_entry(gbcllr::Machine& machine,uint16_t pc) {
    auto registers=machine.debug_registers();registers.pc=pc;
    machine.debug_registers(registers);
}
