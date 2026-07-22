#include "CPU6502.hpp"

#include "Bus.hpp"

namespace nes {

u8 CPU6502::read(u16 address) { return bus_->cpuRead(address); }
void CPU6502::write(u16 address, u8 data) { bus_->cpuWrite(address, data); }
u8 CPU6502::fetch() { return read(pc++); }
void CPU6502::setFlag(Flag flag, bool value) { status = value ? static_cast<u8>(status | flag) : static_cast<u8>(status & ~flag); }
bool CPU6502::getFlag(Flag flag) const { return (status & flag) != 0; }
void CPU6502::setZN(u8 value) { setFlag(Z, value == 0); setFlag(N, (value & 0x80) != 0); }
void CPU6502::push(u8 value) { write(static_cast<u16>(0x0100 | sp--), value); }
u8 CPU6502::pull() { return read(static_cast<u16>(0x0100 | ++sp)); }
u16 CPU6502::read16(u16 address) { const u8 lo = read(address); return static_cast<u16>(lo | (read(address + 1) << 8)); }
u16 CPU6502::read16Bug(u16 address) { const u8 lo = read(address); const u16 hiAddr = static_cast<u16>((address & 0xff00) | ((address + 1) & 0x00ff)); return static_cast<u16>(lo | (read(hiAddr) << 8)); }

void CPU6502::reset() {
    a = x = y = 0;
    sp = 0xfd;
    status = U | I;
    pc = read16(0xfffc);
    cycles_ = 8;
}

void CPU6502::irq() {
    if (getFlag(I)) {
        return;
    }
    push(static_cast<u8>(pc >> 8));
    push(static_cast<u8>(pc));
    setFlag(B, false);
    setFlag(U, true);
    push(status);
    setFlag(I, true);
    pc = read16(0xfffe);
    cycles_ = 7;
}

void CPU6502::nmi() {
    push(static_cast<u8>(pc >> 8));
    push(static_cast<u8>(pc));
    setFlag(B, false);
    setFlag(U, true);
    push(status);
    setFlag(I, true);
    pc = read16(0xfffa);
    cycles_ = 8;
}

u16 CPU6502::imm() { return pc++; }
u16 CPU6502::zp() { return fetch(); }
u16 CPU6502::zpx() { return static_cast<u8>(fetch() + x); }
u16 CPU6502::zpy() { return static_cast<u8>(fetch() + y); }
u16 CPU6502::abs() { const u8 lo = fetch(); return static_cast<u16>(lo | (fetch() << 8)); }
u16 CPU6502::ind() { return read16Bug(abs()); }
u16 CPU6502::indx() { const u8 p = static_cast<u8>(fetch() + x); return static_cast<u16>(read(p) | (read(static_cast<u8>(p + 1)) << 8)); }
u16 CPU6502::indy(bool pageCycle) { const u8 p = fetch(); const u16 base = static_cast<u16>(read(p) | (read(static_cast<u8>(p + 1)) << 8)); const u16 out = static_cast<u16>(base + y); if (pageCycle && (base & 0xff00) != (out & 0xff00)) ++cycles_; return out; }
u16 CPU6502::absx(bool pageCycle) { const u16 base = abs(); const u16 out = static_cast<u16>(base + x); if (pageCycle && (base & 0xff00) != (out & 0xff00)) ++cycles_; return out; }
u16 CPU6502::absy(bool pageCycle) { const u16 base = abs(); const u16 out = static_cast<u16>(base + y); if (pageCycle && (base & 0xff00) != (out & 0xff00)) ++cycles_; return out; }

void CPU6502::branch(bool condition) {
    const i16 offset = static_cast<i16>(static_cast<std::int8_t>(fetch()));
    if (!condition) {
        return;
    }
    ++cycles_;
    const u16 old = pc;
    pc = static_cast<u16>(pc + offset);
    if ((old & 0xff00) != (pc & 0xff00)) {
        ++cycles_;
    }
}

void CPU6502::adc(u8 value) {
    const u16 sum = static_cast<u16>(a + value + (getFlag(C) ? 1 : 0));
    setFlag(C, sum > 0xff);
    setFlag(V, (~(a ^ value) & (a ^ sum) & 0x80) != 0);
    a = static_cast<u8>(sum);
    setZN(a);
}

void CPU6502::sbc(u8 value) { adc(static_cast<u8>(value ^ 0xff)); }

void CPU6502::cmp(u8 lhs, u8 rhs) {
    const u16 t = static_cast<u16>(lhs - rhs);
    setFlag(C, lhs >= rhs);
    setZN(static_cast<u8>(t));
}

u8 CPU6502::aslValue(u8 value) { setFlag(C, value & 0x80); value <<= 1; setZN(value); return value; }
u8 CPU6502::lsrValue(u8 value) { setFlag(C, value & 0x01); value >>= 1; setZN(value); return value; }
u8 CPU6502::rolValue(u8 value) { const bool c = getFlag(C); setFlag(C, value & 0x80); value = static_cast<u8>((value << 1) | (c ? 1 : 0)); setZN(value); return value; }
u8 CPU6502::rorValue(u8 value) { const bool c = getFlag(C); setFlag(C, value & 0x01); value = static_cast<u8>((value >> 1) | (c ? 0x80 : 0)); setZN(value); return value; }

void CPU6502::clock() {
    if (cycles_ > 0) {
        --cycles_;
        return;
    }

    opcode_ = fetch();
    setFlag(U, true);

    auto lda = [&](u16 addr) { a = read(addr); setZN(a); };
    auto ldx = [&](u16 addr) { x = read(addr); setZN(x); };
    auto ldy = [&](u16 addr) { y = read(addr); setZN(y); };
    auto sta = [&](u16 addr) { write(addr, a); };
    auto stx = [&](u16 addr) { write(addr, x); };
    auto sty = [&](u16 addr) { write(addr, y); };
    auto bit = [&](u16 addr) { const u8 v = read(addr); setFlag(Z, (a & v) == 0); setFlag(V, v & 0x40); setFlag(N, v & 0x80); };
    auto inc = [&](u16 addr) { const u8 v = static_cast<u8>(read(addr) + 1); write(addr, v); setZN(v); };
    auto dec = [&](u16 addr) { const u8 v = static_cast<u8>(read(addr) - 1); write(addr, v); setZN(v); };
    auto jump = [&](u16 addr) { pc = addr; };
    auto jsr = [&] { const u16 target = abs(); --pc; push(static_cast<u8>(pc >> 8)); push(static_cast<u8>(pc)); pc = target; };
    auto rts = [&] { const u8 lo = pull(); const u8 hi = pull(); pc = static_cast<u16>((hi << 8) | lo); ++pc; };
    auto brk = [&] { ++pc; push(static_cast<u8>(pc >> 8)); push(static_cast<u8>(pc)); setFlag(B, true); push(status); setFlag(I, true); pc = read16(0xfffe); };
    auto rti = [&] { status = pull(); status &= static_cast<u8>(~B); status |= U; const u8 lo = pull(); const u8 hi = pull(); pc = static_cast<u16>((hi << 8) | lo); };
    auto ora = [&](u16 addr) { a |= read(addr); setZN(a); };
    auto andOp = [&](u16 addr) { a &= read(addr); setZN(a); };
    auto eor = [&](u16 addr) { a ^= read(addr); setZN(a); };
    auto slo = [&](u16 addr) { const u8 v = aslValue(read(addr)); write(addr, v); a |= v; setZN(a); };
    auto rla = [&](u16 addr) { const u8 v = rolValue(read(addr)); write(addr, v); a &= v; setZN(a); };
    auto sre = [&](u16 addr) { const u8 v = lsrValue(read(addr)); write(addr, v); a ^= v; setZN(a); };
    auto rra = [&](u16 addr) { const u8 v = rorValue(read(addr)); write(addr, v); adc(v); };
    auto sax = [&](u16 addr) { write(addr, static_cast<u8>(a & x)); };
    auto lax = [&](u16 addr) { a = x = read(addr); setZN(a); };
    auto dcp = [&](u16 addr) { const u8 v = static_cast<u8>(read(addr) - 1); write(addr, v); cmp(a, v); };
    auto isc = [&](u16 addr) { const u8 v = static_cast<u8>(read(addr) + 1); write(addr, v); sbc(v); };

    switch (opcode_) {
    case 0x00: cycles_ = 7; brk(); break;
    case 0x01: cycles_ = 6; ora(indx()); break;
    case 0x05: cycles_ = 3; ora(zp()); break;
    case 0x06: cycles_ = 5; { const u16 a0 = zp(); write(a0, aslValue(read(a0))); } break;
    case 0x08: cycles_ = 3; push(status | B | U); break;
    case 0x09: cycles_ = 2; ora(imm()); break;
    case 0x0a: cycles_ = 2; a = aslValue(a); break;
    case 0x0d: cycles_ = 4; ora(abs()); break;
    case 0x0e: cycles_ = 6; { const u16 a0 = abs(); write(a0, aslValue(read(a0))); } break;
    case 0x10: cycles_ = 2; branch(!getFlag(N)); break;
    case 0x11: cycles_ = 5; ora(indy(true)); break;
    case 0x15: cycles_ = 4; ora(zpx()); break;
    case 0x16: cycles_ = 6; { const u16 a0 = zpx(); write(a0, aslValue(read(a0))); } break;
    case 0x18: cycles_ = 2; setFlag(C, false); break;
    case 0x19: cycles_ = 4; ora(absy(true)); break;
    case 0x1d: cycles_ = 4; ora(absx(true)); break;
    case 0x1e: cycles_ = 7; { const u16 a0 = absx(false); write(a0, aslValue(read(a0))); } break;
    case 0x20: cycles_ = 6; jsr(); break;
    case 0x21: cycles_ = 6; andOp(indx()); break;
    case 0x24: cycles_ = 3; bit(zp()); break;
    case 0x25: cycles_ = 3; andOp(zp()); break;
    case 0x26: cycles_ = 5; { const u16 a0 = zp(); write(a0, rolValue(read(a0))); } break;
    case 0x28: cycles_ = 4; status = static_cast<u8>((pull() & ~B) | U); break;
    case 0x29: cycles_ = 2; andOp(imm()); break;
    case 0x2a: cycles_ = 2; a = rolValue(a); break;
    case 0x2c: cycles_ = 4; bit(abs()); break;
    case 0x2d: cycles_ = 4; andOp(abs()); break;
    case 0x2e: cycles_ = 6; { const u16 a0 = abs(); write(a0, rolValue(read(a0))); } break;
    case 0x30: cycles_ = 2; branch(getFlag(N)); break;
    case 0x31: cycles_ = 5; andOp(indy(true)); break;
    case 0x35: cycles_ = 4; andOp(zpx()); break;
    case 0x36: cycles_ = 6; { const u16 a0 = zpx(); write(a0, rolValue(read(a0))); } break;
    case 0x38: cycles_ = 2; setFlag(C, true); break;
    case 0x39: cycles_ = 4; andOp(absy(true)); break;
    case 0x3d: cycles_ = 4; andOp(absx(true)); break;
    case 0x3e: cycles_ = 7; { const u16 a0 = absx(false); write(a0, rolValue(read(a0))); } break;
    case 0x40: cycles_ = 6; rti(); break;
    case 0x41: cycles_ = 6; eor(indx()); break;
    case 0x45: cycles_ = 3; eor(zp()); break;
    case 0x46: cycles_ = 5; { const u16 a0 = zp(); write(a0, lsrValue(read(a0))); } break;
    case 0x48: cycles_ = 3; push(a); break;
    case 0x49: cycles_ = 2; eor(imm()); break;
    case 0x4a: cycles_ = 2; a = lsrValue(a); break;
    case 0x4c: cycles_ = 3; jump(abs()); break;
    case 0x4d: cycles_ = 4; eor(abs()); break;
    case 0x4e: cycles_ = 6; { const u16 a0 = abs(); write(a0, lsrValue(read(a0))); } break;
    case 0x50: cycles_ = 2; branch(!getFlag(V)); break;
    case 0x51: cycles_ = 5; eor(indy(true)); break;
    case 0x55: cycles_ = 4; eor(zpx()); break;
    case 0x56: cycles_ = 6; { const u16 a0 = zpx(); write(a0, lsrValue(read(a0))); } break;
    case 0x58: cycles_ = 2; setFlag(I, false); break;
    case 0x59: cycles_ = 4; eor(absy(true)); break;
    case 0x5d: cycles_ = 4; eor(absx(true)); break;
    case 0x5e: cycles_ = 7; { const u16 a0 = absx(false); write(a0, lsrValue(read(a0))); } break;
    case 0x60: cycles_ = 6; rts(); break;
    case 0x61: cycles_ = 6; adc(read(indx())); break;
    case 0x65: cycles_ = 3; adc(read(zp())); break;
    case 0x66: cycles_ = 5; { const u16 a0 = zp(); write(a0, rorValue(read(a0))); } break;
    case 0x68: cycles_ = 4; a = pull(); setZN(a); break;
    case 0x69: cycles_ = 2; adc(read(imm())); break;
    case 0x6a: cycles_ = 2; a = rorValue(a); break;
    case 0x6c: cycles_ = 5; jump(ind()); break;
    case 0x6d: cycles_ = 4; adc(read(abs())); break;
    case 0x6e: cycles_ = 6; { const u16 a0 = abs(); write(a0, rorValue(read(a0))); } break;
    case 0x70: cycles_ = 2; branch(getFlag(V)); break;
    case 0x71: cycles_ = 5; adc(read(indy(true))); break;
    case 0x75: cycles_ = 4; adc(read(zpx())); break;
    case 0x76: cycles_ = 6; { const u16 a0 = zpx(); write(a0, rorValue(read(a0))); } break;
    case 0x78: cycles_ = 2; setFlag(I, true); break;
    case 0x79: cycles_ = 4; adc(read(absy(true))); break;
    case 0x7d: cycles_ = 4; adc(read(absx(true))); break;
    case 0x7e: cycles_ = 7; { const u16 a0 = absx(false); write(a0, rorValue(read(a0))); } break;
    case 0x81: cycles_ = 6; sta(indx()); break;
    case 0x84: cycles_ = 3; sty(zp()); break;
    case 0x85: cycles_ = 3; sta(zp()); break;
    case 0x86: cycles_ = 3; stx(zp()); break;
    case 0x88: cycles_ = 2; --y; setZN(y); break;
    case 0x8a: cycles_ = 2; a = x; setZN(a); break;
    case 0x8c: cycles_ = 4; sty(abs()); break;
    case 0x8d: cycles_ = 4; sta(abs()); break;
    case 0x8e: cycles_ = 4; stx(abs()); break;
    case 0x90: cycles_ = 2; branch(!getFlag(C)); break;
    case 0x91: cycles_ = 6; sta(indy(false)); break;
    case 0x94: cycles_ = 4; sty(zpx()); break;
    case 0x95: cycles_ = 4; sta(zpx()); break;
    case 0x96: cycles_ = 4; stx(zpy()); break;
    case 0x98: cycles_ = 2; a = y; setZN(a); break;
    case 0x99: cycles_ = 5; sta(absy(false)); break;
    case 0x9a: cycles_ = 2; sp = x; break;
    case 0x9d: cycles_ = 5; sta(absx(false)); break;
    case 0xa0: cycles_ = 2; ldy(imm()); break;
    case 0xa1: cycles_ = 6; lda(indx()); break;
    case 0xa2: cycles_ = 2; ldx(imm()); break;
    case 0xa4: cycles_ = 3; ldy(zp()); break;
    case 0xa5: cycles_ = 3; lda(zp()); break;
    case 0xa6: cycles_ = 3; ldx(zp()); break;
    case 0xa8: cycles_ = 2; y = a; setZN(y); break;
    case 0xa9: cycles_ = 2; lda(imm()); break;
    case 0xaa: cycles_ = 2; x = a; setZN(x); break;
    case 0xac: cycles_ = 4; ldy(abs()); break;
    case 0xad: cycles_ = 4; lda(abs()); break;
    case 0xae: cycles_ = 4; ldx(abs()); break;
    case 0xb0: cycles_ = 2; branch(getFlag(C)); break;
    case 0xb1: cycles_ = 5; lda(indy(true)); break;
    case 0xb4: cycles_ = 4; ldy(zpx()); break;
    case 0xb5: cycles_ = 4; lda(zpx()); break;
    case 0xb6: cycles_ = 4; ldx(zpy()); break;
    case 0xb8: cycles_ = 2; setFlag(V, false); break;
    case 0xb9: cycles_ = 4; lda(absy(true)); break;
    case 0xba: cycles_ = 2; x = sp; setZN(x); break;
    case 0xbc: cycles_ = 4; ldy(absx(true)); break;
    case 0xbd: cycles_ = 4; lda(absx(true)); break;
    case 0xbe: cycles_ = 4; ldx(absy(true)); break;
    case 0xc0: cycles_ = 2; cmp(y, read(imm())); break;
    case 0xc1: cycles_ = 6; cmp(a, read(indx())); break;
    case 0xc4: cycles_ = 3; cmp(y, read(zp())); break;
    case 0xc5: cycles_ = 3; cmp(a, read(zp())); break;
    case 0xc6: cycles_ = 5; dec(zp()); break;
    case 0xc8: cycles_ = 2; ++y; setZN(y); break;
    case 0xc9: cycles_ = 2; cmp(a, read(imm())); break;
    case 0xca: cycles_ = 2; --x; setZN(x); break;
    case 0xcc: cycles_ = 4; cmp(y, read(abs())); break;
    case 0xcd: cycles_ = 4; cmp(a, read(abs())); break;
    case 0xce: cycles_ = 6; dec(abs()); break;
    case 0xd0: cycles_ = 2; branch(!getFlag(Z)); break;
    case 0xd1: cycles_ = 5; cmp(a, read(indy(true))); break;
    case 0xd5: cycles_ = 4; cmp(a, read(zpx())); break;
    case 0xd6: cycles_ = 6; dec(zpx()); break;
    case 0xd8: cycles_ = 2; setFlag(D, false); break;
    case 0xd9: cycles_ = 4; cmp(a, read(absy(true))); break;
    case 0xdd: cycles_ = 4; cmp(a, read(absx(true))); break;
    case 0xde: cycles_ = 7; dec(absx(false)); break;
    case 0xe0: cycles_ = 2; cmp(x, read(imm())); break;
    case 0xe1: cycles_ = 6; sbc(read(indx())); break;
    case 0xe4: cycles_ = 3; cmp(x, read(zp())); break;
    case 0xe5: cycles_ = 3; sbc(read(zp())); break;
    case 0xe6: cycles_ = 5; inc(zp()); break;
    case 0xe8: cycles_ = 2; ++x; setZN(x); break;
    case 0xe9: cycles_ = 2; sbc(read(imm())); break;
    case 0xea: cycles_ = 2; break;
    case 0xec: cycles_ = 4; cmp(x, read(abs())); break;
    case 0xed: cycles_ = 4; sbc(read(abs())); break;
    case 0xee: cycles_ = 6; inc(abs()); break;
    case 0xf0: cycles_ = 2; branch(getFlag(Z)); break;
    case 0xf1: cycles_ = 5; sbc(read(indy(true))); break;
    case 0xf5: cycles_ = 4; sbc(read(zpx())); break;
    case 0xf6: cycles_ = 6; inc(zpx()); break;
    case 0xf8: cycles_ = 2; setFlag(D, true); break;
    case 0xf9: cycles_ = 4; sbc(read(absy(true))); break;
    case 0xfd: cycles_ = 4; sbc(read(absx(true))); break;
    case 0xfe: cycles_ = 7; inc(absx(false)); break;
    case 0x03: cycles_ = 8; slo(indx()); break;
    case 0x07: cycles_ = 5; slo(zp()); break;
    case 0x0f: cycles_ = 6; slo(abs()); break;
    case 0x13: cycles_ = 8; slo(indy(false)); break;
    case 0x17: cycles_ = 6; slo(zpx()); break;
    case 0x1b: cycles_ = 7; slo(absy(false)); break;
    case 0x1f: cycles_ = 7; slo(absx(false)); break;
    case 0x23: cycles_ = 8; rla(indx()); break;
    case 0x27: cycles_ = 5; rla(zp()); break;
    case 0x2f: cycles_ = 6; rla(abs()); break;
    case 0x33: cycles_ = 8; rla(indy(false)); break;
    case 0x37: cycles_ = 6; rla(zpx()); break;
    case 0x3b: cycles_ = 7; rla(absy(false)); break;
    case 0x3f: cycles_ = 7; rla(absx(false)); break;
    case 0x43: cycles_ = 8; sre(indx()); break;
    case 0x47: cycles_ = 5; sre(zp()); break;
    case 0x4f: cycles_ = 6; sre(abs()); break;
    case 0x53: cycles_ = 8; sre(indy(false)); break;
    case 0x57: cycles_ = 6; sre(zpx()); break;
    case 0x5b: cycles_ = 7; sre(absy(false)); break;
    case 0x5f: cycles_ = 7; sre(absx(false)); break;
    case 0x63: cycles_ = 8; rra(indx()); break;
    case 0x67: cycles_ = 5; rra(zp()); break;
    case 0x6f: cycles_ = 6; rra(abs()); break;
    case 0x73: cycles_ = 8; rra(indy(false)); break;
    case 0x77: cycles_ = 6; rra(zpx()); break;
    case 0x7b: cycles_ = 7; rra(absy(false)); break;
    case 0x7f: cycles_ = 7; rra(absx(false)); break;
    case 0x83: cycles_ = 6; sax(indx()); break;
    case 0x87: cycles_ = 3; sax(zp()); break;
    case 0x8f: cycles_ = 4; sax(abs()); break;
    case 0x97: cycles_ = 4; sax(zpy()); break;
    case 0xa3: cycles_ = 6; lax(indx()); break;
    case 0xa7: cycles_ = 3; lax(zp()); break;
    case 0xaf: cycles_ = 4; lax(abs()); break;
    case 0xb3: cycles_ = 5; lax(indy(true)); break;
    case 0xb7: cycles_ = 4; lax(zpy()); break;
    case 0xbf: cycles_ = 4; lax(absy(true)); break;
    case 0xc3: cycles_ = 8; dcp(indx()); break;
    case 0xc7: cycles_ = 5; dcp(zp()); break;
    case 0xcf: cycles_ = 6; dcp(abs()); break;
    case 0xd3: cycles_ = 8; dcp(indy(false)); break;
    case 0xd7: cycles_ = 6; dcp(zpx()); break;
    case 0xdb: cycles_ = 7; dcp(absy(false)); break;
    case 0xdf: cycles_ = 7; dcp(absx(false)); break;
    case 0xe3: cycles_ = 8; isc(indx()); break;
    case 0xe7: cycles_ = 5; isc(zp()); break;
    case 0xef: cycles_ = 6; isc(abs()); break;
    case 0xf3: cycles_ = 8; isc(indy(false)); break;
    case 0xf7: cycles_ = 6; isc(zpx()); break;
    case 0xfb: cycles_ = 7; isc(absy(false)); break;
    case 0xff: cycles_ = 7; isc(absx(false)); break;
    case 0x0b:
    case 0x2b:
        cycles_ = 2;
        a = static_cast<u8>(a & read(imm()));
        setZN(a);
        setFlag(C, getFlag(N));
        break;
    case 0x4b:
        cycles_ = 2;
        a = lsrValue(static_cast<u8>(a & read(imm())));
        break;
    case 0x6b: {
        cycles_ = 2;
        a = static_cast<u8>(a & read(imm()));
        const bool oldCarry = getFlag(C);
        a = static_cast<u8>((a >> 1) | (oldCarry ? 0x80 : 0));
        setZN(a);
        setFlag(C, (a & 0x40) != 0);
        setFlag(V, ((a >> 6) ^ (a >> 5)) & 1);
        break;
    }
    case 0xcb: {
        cycles_ = 2;
        const u16 value = static_cast<u16>((a & x) - read(imm()));
        x = static_cast<u8>(value);
        setFlag(C, value < 0x100);
        setZN(x);
        break;
    }
    case 0x1a:
    case 0x3a:
    case 0x5a:
    case 0x7a:
    case 0xda:
    case 0xfa:
        cycles_ = 2;
        break;
    case 0x80:
    case 0x82:
    case 0x89:
    case 0xc2:
    case 0xe2:
        cycles_ = 2;
        (void)imm();
        break;
    case 0x04:
    case 0x44:
    case 0x64:
        cycles_ = 3;
        (void)zp();
        break;
    case 0x14:
    case 0x34:
    case 0x54:
    case 0x74:
    case 0xd4:
    case 0xf4:
        cycles_ = 4;
        (void)zpx();
        break;
    case 0x0c:
        cycles_ = 4;
        (void)abs();
        break;
    case 0x1c:
    case 0x3c:
    case 0x5c:
    case 0x7c:
    case 0xdc:
    case 0xfc:
        cycles_ = 4;
        (void)absx(true);
        break;
    default:
        cycles_ = 2;
        break;
    }

    setFlag(U, true);
    --cycles_;
}

} // namespace nes
