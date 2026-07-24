#pragma once

#include "Types.hpp"

#include <string>

namespace nes {

class Bus;

class CPU6502 {
public:
    // Attach the CPU to the system bus used for all memory and register accesses.
    void connect(Bus* bus) { bus_ = bus; }
    // Initialize registers from the reset vector at $FFFC/$FFFD.
    void reset();
    // Enter maskable interrupt sequence if the interrupt-disable flag allows it.
    void irq();
    // Enter non-maskable interrupt sequence.
    void nmi();
    // Execute one CPU cycle; fetch/decode happens when the previous instruction completes.
    void clock();
    // True when the current instruction has consumed all of its cycles.
    bool complete() const { return cycles_ == 0; }

    // Public registers are exposed for tests and debugging traces.
    u8 a = 0;
    u8 x = 0;
    u8 y = 0;
    u8 sp = 0xfd;
    u16 pc = 0;
    u8 status = 0x24;

private:
    enum Flag : u8 {
        C = 1 << 0,
        Z = 1 << 1,
        I = 1 << 2,
        D = 1 << 3,
        B = 1 << 4,
        U = 1 << 5,
        V = 1 << 6,
        N = 1 << 7,
    };

    // Bus helpers keep instruction implementations independent from memory-map details.
    u8 read(u16 address);
    void write(u16 address, u8 data);
    // Fetch the operand byte pointed to by the current addressing mode.
    u8 fetch();
    void setFlag(Flag flag, bool value);
    bool getFlag(Flag flag) const;
    // Update zero and negative flags from an 8-bit result.
    void setZN(u8 value);
    void push(u8 value);
    u8 pull();
    // 16-bit reads, including the original 6502 indirect-JMP page-wrap bug.
    u16 read16(u16 address);
    u16 read16Bug(u16 address);
    void branch(bool condition);

    // Addressing mode helpers return the effective address for the current instruction.
    u16 imm();
    u16 zp();
    u16 zpx();
    u16 zpy();
    u16 abs();
    u16 absx(bool pageCycle);
    u16 absy(bool pageCycle);
    u16 ind();
    u16 indx();
    u16 indy(bool pageCycle);

    void adc(u8 value);
    void sbc(u8 value);
    void cmp(u8 lhs, u8 rhs);
    u8 aslValue(u8 value);
    u8 lsrValue(u8 value);
    u8 rolValue(u8 value);
    u8 rorValue(u8 value);

    Bus* bus_ = nullptr;
    u8 cycles_ = 0;
    u8 opcode_ = 0;
};

} // namespace nes
