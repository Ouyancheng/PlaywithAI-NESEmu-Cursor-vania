#pragma once

#include "Types.hpp"

#include <string>

namespace nes {

class Bus;

class CPU6502 {
public:
    void connect(Bus* bus) { bus_ = bus; }
    void reset();
    void irq();
    void nmi();
    void clock();
    bool complete() const { return cycles_ == 0; }

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

    u8 read(u16 address);
    void write(u16 address, u8 data);
    u8 fetch();
    void setFlag(Flag flag, bool value);
    bool getFlag(Flag flag) const;
    void setZN(u8 value);
    void push(u8 value);
    u8 pull();
    u16 read16(u16 address);
    u16 read16Bug(u16 address);
    void branch(bool condition);

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
