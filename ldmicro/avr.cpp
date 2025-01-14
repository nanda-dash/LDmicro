//-----------------------------------------------------------------------------
// Copyright 2007 Jonathan Westhues
//
// This file is part of LDmicro.
//
// LDmicro is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// LDmicro is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with LDmicro.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// An AVR assembler, for our own internal use, plus routines to generate
// code from the ladder logic structure, plus routines to generate the
// runtime needed to schedule the cycles.
// Jonathan Westhues, Oct 2004
//-----------------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "ldmicro.h"
#include "intcode.h"

#define BIT0 0
#define BIT1 1
#define BIT2 2
#define BIT3 3
#define BIT4 4
#define BIT5 5
#define BIT6 6
#define BIT7 7

#define r0 0 // used muls. Don't use elsewhere!!!
#define r1 1 // used muls. Don't use elsewhere!!!
#define r2 2 // used in MultiplyRoutine
#define r3 3 // used in CopyBit, XorBit, _SWAP etc.

#define r5 5
#define r7 7 // used as Sign Register (BIT7) in DivideRoutine
#define r9 9 // used ONLY in QuadEncodInterrupt to save REG_SREG. Don't use elsewhere!!!

#define r10 10 //used as op1 copy in MultiplyRoutine24
#define r11 11
#define r12 12
#define r13 13

#define r14 14
#define r15 15
#define r16 16 //used as op2
#define r17 17 //...
#define r18 18 //...
#define r19 19 //used as op2+3

#define r20 20 //used as op1 and result
#define r21 21 //...
#define r22 22 //...
#define r23 23 //used as op1+3 and result+3

#define r24 24
#define r25 25 // used in WriteMemory macro, CopyBit, SetBit, IfBitClear, etc.
//      r25    // used as Loop counter in MultiplyRoutine, DivideRoutine, etc.

#define r26 26 // X
#define r27 27
#define r28 28 // Y used as pointer to op2
#define r29 29
#define r30 30 // Z used as pointer to op1 and result
#define r31 31

/* Pointer definition   */
#define XL      r26
#define XH      r27
#define YL      r28
#define YH      r29
#define ZL      r30
#define ZH      r31

#define rX  r26
#define Xlo r26
#define Xhi r27

#define rY  r28
#define Ylo r28
#define Yhi r29

#define rZ  r30
#define Zlo r30
#define Zhi r31

// not complete; just what I need
typedef enum AvrOpTag {
    OP_VACANT,
    OP_ADC,
    OP_ADD,
    OP_ADIW,
    OP_SBIW,
    OP_ASR,
    OP_BRCC,
    OP_BRCS,
    OP_BREQ,
    OP_BRGE,
    OP_BRLO,
    OP_BRLT,
    OP_BRNE,
    OP_BRMI,
    OP_CBR,
    OP_CLC,
    OP_CLR,
    OP_SER,
    OP_COM,
    OP_CP,
    OP_CPC,
    OP_CPI,
    OP_DEC,
    OP_EOR,
    OP_ICALL,
    OP_IJMP,
    OP_INC,
    OP_LDI,
    OP_LD_X,
    OP_LD_XP,  /*post increment X+*/
    OP_LD_XS,  /*-X pre decrement*/
    OP_LD_Y,
    OP_LD_YP,  /*post increment Y+*/
    OP_LD_YS,  /*-Y pre decrement*/
    OP_LDD_Y,  // Y+q
    OP_LD_Z,
    OP_LD_ZP,  /*post increment Z+*/
    OP_LD_ZS,  /*-Z pre decrement*/
    OP_LDD_Z,  // Z+q
    OP_LPM_0Z,/* R0 <- (Z)*/
    OP_LPM_Z, /* Rd <- (Z)*/
    OP_LPM_ZP,/* Rd <- (Z++) post incterment */
    OP_DB, // one byte in flash word, hi byte = 0!
    OP_DB2,// two bytes in flash word
    OP_DW, // word in flash word
    OP_MOV,
    OP_MOVW,
    OP_SWAP,
    OP_RCALL,
    OP_RET,
    OP_RETI,
    OP_RJMP,
    OP_ROR,
    OP_ROL,
    OP_LSL,
    OP_LSR,
    OP_SEC,
    OP_SBC,
    OP_SBCI,
    OP_SBR,
    OP_SBRC,
    OP_SBRS,
    OP_ST_X,
    OP_ST_XP,/*+*/
    OP_ST_XS,/*-*/
    OP_ST_Y,
    OP_ST_YP,/*+*/
    OP_ST_YS,/*-*/
    OP_ST_Z,
    OP_ST_ZP,/*+*/
    OP_ST_ZS,/*-*/
    OP_SUB,
    OP_SUBI,
    OP_TST,
    OP_WDR,
    OP_AND,
    OP_ANDI,
    OP_OR,
    OP_ORI,
    OP_CPSE,
    #ifdef USE_MUL
    OP_MUL,
    OP_MULS,
    OP_MULSU,
    #endif
    #ifdef USE_IO_REGISTERS
    OP_IN,
    OP_OUT,
    OP_SBI,
    OP_CBI,
    OP_SBIC,
    OP_SBIS,
    #endif
    OP_PUSH,
    OP_POP,
    OP_CLI,
    OP_SEI,
    OP_NOP,
    OP_COMMENT
} AvrOp;

typedef struct AvrInstructionTag {
    AvrOp       op;
    DWORD       arg1;
    DWORD       arg2;
    char        comment[MAX_COMMENT_LEN];
    int         rung;  // This AvrInstruction located in Prog.rungs[rung] LD
    int         IntPc; // This AvrInstruction located in IntCode[IntPc]
} AvrInstruction;

#define MAX_PROGRAM_LEN 128*1024
static AvrInstruction AvrProg[MAX_PROGRAM_LEN];
static DWORD AvrProgWriteP;

static int IntPcNow = -INT_MAX; //must be static

typedef struct RungAddrTag {
    DWORD   KnownAddr; // Addres to jump to the start of rung abowe the current in LD
    DWORD   FwdAddr;   // Addres to jump to the start of rung below the current in LD
} RungAddr;
RungAddr AddrOfRungN[MAX_RUNGS];

#define OP_XOR OP_EOR

// For yet unresolved references in jumps
static DWORD FwdAddrCount;

// Fancier: can specify a forward reference to the high or low octet of a
// 16-bit address, which is useful for indirect jumps.
#define FWD_LO(x) ((x) | 0x20000000)
#define FWD_HI(x) ((x) | 0x40000000)

// Address to jump to when we finish one PLC cycle
static DWORD BeginningOfCycleAddr;

// Address of the multiply subroutine, and whether we will have to include it
static DWORD MultiplyAddress;
static BOOL MultiplyUsed;
// and also divide
static DWORD DivideAddress;
static BOOL DivideUsed;

// For EEPROM: we queue up characters to send in 16-bit words (corresponding
// to the integer variables), but we can actually just program 8 bits at a
// time, so we need to store the high byte somewhere while we wait.
static DWORD EepromHighByte;
static DWORD EepromHighByteWaitingAddr;
static int EepromHighByteWaitingBit;

// Some useful registers, unfortunately many of which are in different places
// on different AVRs! I consider this a terrible design choice by Atmel.
// '-1' means not defined.
//static DWORD REG_TIMSK  = -1;
static BYTE      OCIE1A = -1; // Timer/Counter1, Output Compare A Match Interrupt Enable
static BYTE      TOIE1  = -1; // Timer/Counter1 Overflow Interrupt Enable
static BYTE      TOIE0  = -1; // Timer/Counter0 Overflow Interrupt Enable

static DWORD REG_TIFR1  = -1;
static DWORD REG_TIFR0  = -1;
static BYTE      OCF1A  = -1; // Timer/Counter1, Output Compare A Match Flag
static BYTE      TOV1   = -1; // Timer/Counter1 Overflow Flag
static BYTE      TOV0   = -1; // Timer/Counter0 Overflow Flag
//#define REG_OCR1AH  0x4b
//#define REG_OCR1AL  0x4a
//#define REG_TCCR1A  0x4f
//#define REG_TCCR1B  0x4e

#define REG_SREG    0x5f
#define     SREG_C  0
#define     SREG_Z  1
#define     SREG_N  2
#define     SREG_V  3
#define     SREG_S  4
#define     SREG_H  5
#define     SREG_T  6
#define     SREG_I  7
#define REG_SPH     0x5e
#define REG_SPL     0x5d

static DWORD REG_ADMUX  = 0x27;
static DWORD REG_ADCSRA = 0x26;
#define          ADEN     BIT7
#define          ADSC     BIT6
#define          ADFR     BIT5 // ADATE
#define          ADIE     BIT3
static DWORD REG_ADCH   = 0x25;
static DWORD REG_ADCL   = 0x24;

static DWORD REG_OCR1AH = -1;
static DWORD REG_OCR1AL = -1;
static DWORD REG_TCCR1A = -1;
static DWORD REG_TCCR1B = -1;
#define          WGM13     4
#define          WGM12     3
#define          WGM11     1
#define          WGM10     0
static DWORD REG_UBRRH  = -1;
static DWORD REG_UBRRL  = -1;
static DWORD REG_UCSRC  = -1;
static DWORD REG_UCSRB  = -1;
#define          RXEN   BIT4
#define          TXEN   BIT3
static DWORD REG_UCSRA  = -1;
#define      RXC  BIT7 // USART Receive Complete
                       // This flag bit is set when there are unread data
                       //   in the receive buffer and
                       //   cleared when the receive buffer is empty.
#define      TXC  BIT6 // USART Transmit Complete
#define      UDRE BIT5 // bit is 1 when tx buffer is empty

#define      FE   BIT4 // Frame Error
#define      DOR  BIT3 // Data OverRun
#define      PE   BIT2 // Parity Error

static DWORD REG_UDR = -1;

#define REG_TCCR0   0x53
#define REG_TCNT0   0x52

#define REG_WDTCR   0x41
#define     WDCE    BIT4
#define     WDE     BIT3
#define     WDP2    BIT2
#define     WDP1    BIT1
#define     WDP0    BIT0

//#define REG_OCR2    0x43
//#define REG_TCCR2   0x45
static DWORD REG_OCR2   = 0x43; //-1;
static DWORD REG_TCCR2  = 0x45; //-1;
static DWORD REG_TCCR2B = -1;
static BYTE      WGM20  = BIT6;
static BYTE      WGM21  = BIT3;
static BYTE      COM21  = BIT5;
static BYTE      COM20  = BIT4;

static DWORD REG_EEARH     = 0x3f;
static DWORD REG_EEARL     = 0x3e;
static DWORD REG_EEDR      = 0x3d;
static DWORD REG_EECR      = 0x3c;
#define          EERE   BIT0
#define          EEWE   BIT1
#define          EEMWE  BIT2
#define          EERIE  BIT3

#define IOREG(sfr) (sfr - 0x20)

// I2C support for atmega8,16,32,64,128
#define TWCR    0x56
#define TWDR    0x23
#define TWAR    0x22
#define TWSR    0x21
#define TWBR    0x20
/*
// I2C support for atmega48,88,168,328,164,324,644,1284,2560
#define TWAMR   0xBD
#define TWCR    0xBC
#define TWDR    0xBB
#define TWAR    0xBA
#define TWSR    0xB9
#define TWBR    0xB8
*/

// Interrupt Vectors Table
static DWORD Int0Addr         = -1;
static DWORD Int1Addr         = -1;
static DWORD Timer0OvfAddr    = -1;
static DWORD Timer1OvfAddr    = -1;
static DWORD Timer1CompAAddr  = -1;

//External Interrupts support
static DWORD REG_MCUCR = -1;
static BYTE      ISC00 = -1;
static BYTE      ISC01 = -1;
static BYTE      ISC10 = -1;
static BYTE      ISC11 = -1;

static DWORD REG_GICR  = -1;
static BYTE      INT1  = -1;
static BYTE      INT0  = -1;

static DWORD REG_GIFR  = -1;
static BYTE      INTF1 = -1;
static BYTE      INTF0 = -1;

#ifdef USE_TIMER0_AS_LADDER_CYCLE
//used in PlcCycleTimerOverflowInterrupt for PLC timing
//static DWORD  PlcCycleTimerOverflowVector;
static int    tcnt0PlcCycle = -1;
#else
#endif
//used in NPulseTimerOverflowInterrupt in ELEM_NPULSE
static DWORD  NPulseTimerOverflowVector;
static int    tcntNPulse = -1;
static DWORD  NPulseTimerOverflowRegAddr;
static int    NPulseTimerOverflowBit;
static DWORD  NPulseTimerOverflowCounter;
static int sovNPulseTimerOverflowCounter;


static int IntPc;

static void CompileFromIntermediate(void);

//-----------------------------------------------------------------------------
// Wipe the program and set the write pointer back to the beginning. Also
// flush all the state of the register allocators etc.
//-----------------------------------------------------------------------------
static void WipeMemory(void)
{
    memset(AvrProg, 0, sizeof(AvrProg));
    AvrProgWriteP = 0;
}

//-----------------------------------------------------------------------------
// Store an instruction at the next spot in program memory.  Error condition
// if this spot is already filled. We don't actually assemble to binary yet;
// there may be references to resolve.
//-----------------------------------------------------------------------------
static void Instruction(AvrOp op, DWORD arg1, DWORD arg2, char *comment)
{
    if(AvrProg[AvrProgWriteP].op != OP_VACANT) oops();

    AvrProg[AvrProgWriteP].op = op;
    AvrProg[AvrProgWriteP].arg1 = arg1;
    AvrProg[AvrProgWriteP].arg2 = arg2;
    if(comment) strcpy(AvrProg[AvrProgWriteP].comment, comment);
    AvrProg[AvrProgWriteP].rung = rungNow;
    AvrProg[AvrProgWriteP].IntPc = IntPcNow;
    AvrProgWriteP++;
}

static void Instruction(AvrOp op, DWORD arg1, DWORD arg2)
{
    Instruction(op, arg1, arg2, NULL);
}

static void Instruction(AvrOp op, DWORD arg1)
{
    Instruction(op, arg1, 0);
}

static void Instruction(AvrOp op)
{
    Instruction(op, 0, 0);
}

static void Instruction(AvrOp op, char *comment)
{
    Instruction(op, 0, 0, NULL);
}

//-----------------------------------------------------------------------------
// printf-like comment function
//-----------------------------------------------------------------------------
static void Comment(char *str, ...)
{
    if(strlen(str)>=MAX_COMMENT_LEN)
      str[MAX_COMMENT_LEN-1]='\0';
    va_list f;
    char buf[MAX_COMMENT_LEN];
    va_start(f, str);
    vsprintf(buf, str, f);
    Instruction(OP_COMMENT, buf);
}

//-----------------------------------------------------------------------------
// Allocate a unique descriptor for a forward reference. Later that forward
// reference gets assigned to an absolute address, and we can go back and
// fix up the reference.
//-----------------------------------------------------------------------------
static DWORD AllocFwdAddr(void)
{
    FwdAddrCount++;
    return 0x80000000 | FwdAddrCount;
}

//-----------------------------------------------------------------------------
// Go back and fix up the program given that the provided forward address
// corresponds to the next instruction to be assembled.
//-----------------------------------------------------------------------------
static void FwdAddrIsNow(DWORD addr)
{
    if(!(addr & 0x80000000)) oops();

    DWORD i;
    for(i = 0; i < AvrProgWriteP; i++) {
        if(AvrProg[i].arg1 == addr) {
            AvrProg[i].arg1 = AvrProgWriteP;
        } else if(AvrProg[i].arg2 == FWD_LO(addr)) {
            AvrProg[i].arg2 = (AvrProgWriteP & 0xff);
        } else if(AvrProg[i].arg2 == FWD_HI(addr)) {
            AvrProg[i].arg2 = AvrProgWriteP >> 8;
        }
    }
}

//-----------------------------------------------------------------------------
static void AddrCheckForErrorsPostCompile()
{
    DWORD i;
    for(i = 0; i < AvrProgWriteP; i++) {
        if(AvrProg[i].arg1 & 0x80000000) {
            Error("Every AllocFwdAddr needs FwdAddrIsNow.");
            Error("i=%d op=%d arg1=%d arg2=%d", i,
               AvrProg[i].op,
               AvrProg[i].arg1,
               AvrProg[i].arg2);
            CompileError();
        }
    }
}
//-----------------------------------------------------------------------------
// Given an opcode and its operands, assemble the 16-bit instruction for the
// AVR. Check that the operands do not have more bits set than is meaningful;
// it is an internal error if they do not. Needs to know what address it is
// being assembled to so that it generate relative jumps; internal error if
// a relative jump goes out of range.
//-----------------------------------------------------------------------------
static DWORD Assemble(DWORD addrAt, AvrOp op, DWORD arg1, DWORD arg2)
{
#define CHECK(v, bits) if((v) != ((v) & ((1 << (bits))-1))) oops()
#define CHECK2(v, LowerRangeInclusive, UpperRangeInclusive) if( (v<LowerRangeInclusive) || (v > UpperRangeInclusive) ) ooops("v=%d [%d..%d]", v, LowerRangeInclusive, UpperRangeInclusive)
    switch(op) {
    case OP_COMMENT:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0;

    case OP_NOP:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0;

    case OP_ASR:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9000 | (2 << 9) | (arg1 << 4) | 5;

    case OP_ROR:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 12) | (2 << 9) | (arg1 << 4) | 7;

    case OP_ADD:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (3 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_ADC:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (7 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_ADIW:
        if(!((arg1==24)||(arg1==26)||(arg1==28)||(arg1==30))) oops();
        CHECK2(arg2, 0, 64);
        return 0x9600 | ((arg2 & 0x30) << 2) | ((arg1 & 0x06) << 3) |
            (arg2 & 0x0f);

    case OP_SBIW:
        if(!((arg1==24)||(arg1==26)||(arg1==28)||(arg1==30))) oops();
        CHECK2(arg2, 0, 64);
        return 0x9700 | ((arg2 & 0x30) << 2) | ((arg1 & 0x06) << 3) |
            (arg2 & 0x0f);

    case OP_EOR:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (9 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_CLR:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 10) | ((arg1 & 0x10) << 5) | (arg1 << 4) |
            (arg1 & 0x0f);

    case OP_SER:
        CHECK2(arg1,16,31);
        CHECK(arg1, 5); CHECK(arg2, 0);
        if((arg1<16) || (31<arg1)) oops();
        return 0xEF0F | (arg1 << 4);

    case OP_SUB:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (6 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_SBC:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (2 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_CP:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (5 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_CPC:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (1 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_CPI:
        CHECK2(arg1,16,31); CHECK(arg2, 8);
        return 0x3000 | ((arg2 & 0xF0) << 4) | ((arg1 & 0x0F) << 4) |
            (arg2 & 0x0F);

    case OP_COM:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 12) | (2 << 9) | (arg1 << 4);

    case OP_SBR:
        CHECK(arg1, 5); CHECK(arg2, 8);
        if(!(arg1 & 0x10)) oops();
        arg1 &= ~0x10;
        return (6 << 12) | ((arg2 & 0xf0) << 4) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_CBR:
        CHECK(arg1, 5); CHECK(arg2, 8);
        if(!(arg1 & 0x10)) oops();
        arg1 &= ~0x10;
        arg2 = ~arg2;
        return (7 << 12) | ((arg2 & 0xf0) << 4) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_INC:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (0x4a << 9) | (arg1 << 4) | 3;

    case OP_DEC:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (0x4a << 9) | (arg1 << 4) | 10;

    case OP_SUBI:
        CHECK(arg1, 5); CHECK(arg2, 8);
        if(!(arg1 & 0x10)) oops();
        arg1 &= ~0x10;
        return (5 << 12) | ((arg2 & 0xf0) << 4) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_SBCI:
        CHECK(arg1, 5); CHECK(arg2, 8);
        if(!(arg1 & 0x10)) oops();
        arg1 &= ~0x10;
        return (4 << 12) | ((arg2 & 0xf0) << 4) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_TST:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (8 << 10) | ((arg1 & 0x10) << 4) | ((arg1 & 0x10) << 5) |
            ((arg1 & 0xf) << 4) | (arg1 & 0xf);

    case OP_SEC:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x9408;

    case OP_CLC:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x9488;

    case OP_IJMP:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x9409;

    case OP_ICALL:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x9509;

    case OP_RJMP:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 2047 || ((int)arg1) < -2048) oops();
        arg1 &= (4096-1);
        return (12 << 12) | arg1;

    case OP_RCALL:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 2047 || ((int)arg1) < -2048) oops();
        arg1 &= (4096-1);
        return (13 << 12) | arg1;

    case OP_RETI:
        return 0x9518;

    case OP_RET:
        return 0x9508;

    case OP_SBRC:
        CHECK(arg1, 5); CHECK(arg2, 3);
        return (0x7e << 9) | (arg1 << 4) | arg2;

    case OP_SBRS:
        CHECK(arg1, 5); CHECK(arg2, 3);
        return (0x7f << 9) | (arg1 << 4) | arg2;

    case OP_BREQ:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (arg1 << 3) | 1;

    case OP_BRNE:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (1 << 10) | (arg1 << 3) | 1;

    case OP_BRLO:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (arg1 << 3);

    case OP_BRGE:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (1 << 10) | (arg1 << 3) | 4;

    case OP_BRLT:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (arg1 << 3) | 4;

    case OP_BRCC:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (1 << 10) | (arg1 << 3);

    case OP_BRCS:
        CHECK(arg2, 0);
        arg1 = arg1 - addrAt - 1;
        if(((int)arg1) > 63 || ((int)arg1) < -64) oops();
        arg1 &= (128-1);
        return (0xf << 12) | (arg1 << 3);

    case OP_MOV:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (0xb << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_LDI:
        CHECK(arg1, 5); CHECK(arg2, 8);
        if(!(arg1 & 0x10)) oops();
        arg1 &= ~0x10;
        return (0xe << 12) | ((arg2 & 0xf0) << 4) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_LD_X:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 12) | (arg1 << 4) | 12;

    case OP_LD_XP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 12) | (arg1 << 4) | 13;

    case OP_LD_XS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (9 << 12) | (arg1 << 4) | 14;
    case OP_LD_Y:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x8000 | (arg1 << 4) |  8;

    case OP_LD_YP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9000 | (arg1 << 4) |  9;

    case OP_LD_YS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9000 | (arg1 << 4) | 10;

    case OP_LDD_Y:
        CHECK(arg1, 5); CHECK(arg2, 6);
        return 0x8008 | (arg1 << 4) | ((arg2 & 0x20) << 8) | ((arg2 & 0x18) << 7) | (arg2 & 0x7);

    case OP_LD_Z:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x8000 | (arg1 << 4) |  0;

    case OP_LD_ZP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9000 | (arg1 << 4) |  1;

    case OP_LD_ZS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9000 | (arg1 << 4) |  2;

    case OP_LDD_Z:
        CHECK(arg1, 5); CHECK(arg2, 6);
        return 0x8000 | (arg1 << 4) | ((arg2 & 0x20) << 8) | ((arg2 & 0x18) << 7) | (arg2 & 0x7);

    case OP_LPM_0Z:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x95C8;

    case OP_LPM_Z:
        CHECK2(arg1, 0, 31); CHECK(arg2, 0);
        return (0x9004) | (arg1 << 4);

    case OP_LPM_ZP:
        CHECK2(arg1, 0, 31); CHECK(arg2, 0);
        return (0x9005) | (arg1 << 4);

    case OP_ST_X:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (0x49 << 9) | (arg1 << 4) | 12;

    case OP_ST_XP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (0x49 << 9) | (arg1 << 4) | 13;

    case OP_ST_XS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return (0x49 << 9) | (arg1 << 4) | 14;
    case OP_ST_Y:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x8200 | (arg1 << 4) |  8;

    case OP_ST_YP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9200 | (arg1 << 4) |  9;

    case OP_ST_YS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9200 | (arg1 << 4) | 10;

    case OP_ST_Z:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x8200 | (arg1 << 4) |  0;

    case OP_ST_ZP:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9200 | (arg1 << 4) |  1;

    case OP_ST_ZS:
        CHECK(arg1, 5); CHECK(arg2, 0);
        return 0x9200 | (arg1 << 4) |  2;

    case OP_WDR:
        CHECK(arg1, 0); CHECK(arg2, 0);
        return 0x95a8;

    case OP_ANDI:
        CHECK2(arg1,16,31); CHECK(arg2, 8);
        return 0x7000 | ((arg2 & 0xF0) << 4) | ((arg1 & 0x0F) << 4) | (arg2 & 0x0F);

    case OP_ORI:
        CHECK2(arg1,16,31); CHECK(arg2, 8);
        return 0x6000 | ((arg2 & 0xF0) << 4) | ((arg1 & 0x0F) << 4) | (arg2 & 0x0F);

    case OP_AND:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (0x8 << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    case OP_OR:
        CHECK(arg1, 5); CHECK(arg2, 5);
        return (0xA << 10) | ((arg2 & 0x10) << 5) | (arg1 << 4) |
            (arg2 & 0x0f);

    default:
        oops();
        break;
    }
}

//-----------------------------------------------------------------------------
// Write an intel IHEX format description of the program assembled so far.
// This is where we actually do the assembly to binary format.
//-----------------------------------------------------------------------------
static void WriteHexFile(FILE *f)
{
    BYTE soFar[16];
    int soFarCount = 0;
    DWORD soFarStart = 0;

    DWORD i;
    for(i = 0; i < AvrProgWriteP; i++) {
        DWORD w = Assemble(i, AvrProg[i].op, AvrProg[i].arg1, AvrProg[i].arg2);

        if(soFarCount == 0) soFarStart = i;
        soFar[soFarCount++] = (BYTE)(w & 0xff);
        soFar[soFarCount++] = (BYTE)(w >> 8);

        if(soFarCount >= 0x10 || i == (AvrProgWriteP-1)) {
            StartIhex(f);
            WriteIhex(f, soFarCount);
            WriteIhex(f, (BYTE)((soFarStart*2) >> 8));
            WriteIhex(f, (BYTE)((soFarStart*2) & 0xff));
            WriteIhex(f, 0x00);
            int j;
            for(j = 0; j < soFarCount; j++) {
                WriteIhex(f, soFar[j]);
            }
            FinishIhex(f);
            soFarCount = 0;
        }
    }

    // end of file record
    fprintf(f, ":00000001FF\n");
    if((Prog.mcu->flashWords) && (AvrProgWriteP >= Prog.mcu->flashWords)) {
        Error(_(" Flash program memory size %d is exceed limit %d words\nfor %s."), AvrProgWriteP, Prog.mcu->flashWords, Prog.mcu->mcuName);
    }
    dbp(_("%d(0x%X) is flash program memory size,\n%d(0x%X) words is limit for\n%s."), AvrProgWriteP, AvrProgWriteP, Prog.mcu->flashWords, Prog.mcu->flashWords, Prog.mcu->mcuName);
}

//-----------------------------------------------------------------------------
// Make sure that the given address is loaded in the X register; might not
// have to update all of it.
//-----------------------------------------------------------------------------
static void LoadXAddr(DWORD addr)
//used rX; Opcodes: 2
{
    Instruction(OP_LDI, 26, (addr & 0xff)); // X-register Low Byte
    Instruction(OP_LDI, 27, (addr >> 8));   // X-register High Byte
}

static void LoadYAddr(DWORD addr)
//used rY; Opcodes: 2
{
    Instruction(OP_LDI, 28, (addr & 0xff)); // Y-register Low Byte
    Instruction(OP_LDI, 29, (addr >> 8));   // Y-register High Byte
}

static void LoadZAddr(DWORD addr)
//used ZL; Opcodes: 2
{
    Instruction(OP_LDI, 30, (addr & 0xff)); // Z-register Low Byte
    Instruction(OP_LDI, 31, (addr >> 8));   // Z-register High Byte
}

//-----------------------------------------------------------------------------
// Generate code to write/read an 8-bit value to a particular register.
//-----------------------------------------------------------------------------
static void WriteMemory(DWORD addr, BYTE val)
//used ZL, r25; Opcodes: 4
{
    if(addr <= 0) {
        Error(_("Zero memory addres not allowed!\nWriteMemory(0, %d) skiped!"), val); //see TODO
        return;
    }
    LoadZAddr(addr);
    // load r25 with the data
    Instruction(OP_LDI, r25, val);
    // do the store
    Instruction(OP_ST_ZP, r25);
}
//-----------------------------------------------------------------------------
// Use only after WriteMemory()
//-----------------------------------------------------------------------------
static void WriteMemoryNextAddr(BYTE val)
//used ZL, r25; Opcodes: 2
{
    // load r25 with the data
    Instruction(OP_LDI, r25, val);
    // do the store
    Instruction(OP_ST_ZP, r25);
}
//-----------------------------------------------------------------------------
static void WriteRegToIO(DWORD addr, BYTE reg)
//   used ZL; Opcodes: 3
//or used   ; Opcodes: 1
{
    if(addr <= 0) {
        Error(_("Zero memory addres not allowed!\nWriteRegToIO skiped.")); //see TODO
        return;
    }
    if(reg < 0) {
        Error(_("Registers less zero not allowed!\nWriteRegToIO skiped.")); //see TODO
        return;
    }
    #ifdef USE_IO_REGISTERS
    Instruction(OP_OUT, IOREG(addr), reg);
    #else
    LoadZAddr(addr);
    Instruction(OP_ST_Z, reg);
    #endif
}
//-----------------------------------------------------------------------------
static void ReadIoToReg(BYTE reg, DWORD addr)
//   used ZL; Opcodes: 3
//or used   ; Opcodes: 1
{
    if(addr <= 0) {
        Error(_("Zero memory addres not allowed!\nReadIoToReg skiped.")); //see TODO
        return;
    }
    if(reg < 0) {
        Error(_("Registers less zero not allowed!\nReadIoToReg skiped.")); //see TODO
        return;
    }
    #ifdef USE_IO_REGISTERS
    Instruction(OP_IN, reg, IOREG(addr));
    #else
    LoadZAddr(addr);
    Instruction(OP_LD_Z, reg);
    #endif
}
//-----------------------------------------------------------------------------
// Copy just one bit from one place to another.
//-----------------------------------------------------------------------------
static void CopyBit(DWORD addrDest, int bitDest, DWORD addrSrc, int bitSrc)
{
//used ZL, r25, r3; Opcodes: 11
    LoadZAddr(addrSrc);  Instruction(OP_LD_Z, r3);
    LoadZAddr(addrDest); Instruction(OP_LD_Z, r25);
    Instruction(OP_SBRS, r3, bitSrc);
    Instruction(OP_CBR, r25, (1 << bitDest));
    Instruction(OP_SBRC, r3, bitSrc);
    Instruction(OP_SBR, r25, (1 << bitDest));
    Instruction(OP_ST_Z, r25);
/*
//used ZL, r25; Opcodes: 9
    LoadZAddr(addrSrc);  Instruction(OP_LD_Z, r25);
    Instruction(OP_BST, r25, bitSrc);
    LoadZAddr(addrDest); Instruction(OP_LD_Z, r25);
    Instruction(OP_BLD, r25, bitDest);
    Instruction(OP_ST_Z, r25);
*/
}

//-----------------------------------------------------------------------------
// Execute the next instruction only if the specified bit of the specified
// memory location is clear (i.e. skip if set).
//-----------------------------------------------------------------------------
static void IfBitClear(DWORD addr, int bit, BYTE reg)
{
    LoadZAddr(addr);
    Instruction(OP_LD_Z, reg);
    Instruction(OP_SBRS, reg, bit);
}
static void IfBitClear(DWORD addr, int bit)
//used ZL, r25; Opcodes: 4 // bit in [0..7]
{
    IfBitClear(addr, bit, r25);
}

#ifdef USE_IO_REGISTERS
static void IfBitClearIO(BYTE reg, int bit)
{
    Instruction(OP_SBIS, IOREG(reg), bit);
}
#endif
//-----------------------------------------------------------------------------
// Execute the next instruction only if the specified bit of the specified
// memory location is set (i.e. skip if clear).
//-----------------------------------------------------------------------------
static void IfBitSet(DWORD addr, int bit)
//used ZL, r25 // bit in [0..7]
{
    LoadZAddr(addr);
    Instruction(OP_LD_Z, r25);
    Instruction(OP_SBRC, r25, bit);
}

#ifdef USE_IO_REGISTERS
static void IfBitSetIO(BYTE reg, int bit)
{
    Instruction(OP_SBIC, IOREG(reg), bit);
}
#endif
//-----------------------------------------------------------------------------
// Set a given bit in an arbitrary (not necessarily I/O memory) location in
// memory.
//-----------------------------------------------------------------------------
static void SetBit(DWORD addr, int bit)
//used ZL, r25 // Opcodes: 5
{
    LoadZAddr(addr);
    Instruction(OP_LD_Z, r25);
    Instruction(OP_SBR, r25, (1 << bit));
    Instruction(OP_ST_Z, r25);
}

#ifdef USE_IO_REGISTERS
static void SetBitIO(BYTE reg, int bit)
// Opcodes: 1
{
    Instruction(OP_SBI, IOREG(reg), bit);
}
#endif
//-----------------------------------------------------------------------------
// Clear a given bit in an arbitrary (not necessarily I/O memory) location in
// memory.
//-----------------------------------------------------------------------------
static void ClearBit(DWORD addr, int bit)
//used ZL, r25; Opcodes: 5
{
    LoadZAddr(addr);
    Instruction(OP_LD_Z, r25);
    Instruction(OP_CBR, r25, (1 << bit));
    Instruction(OP_ST_Z, r25);
}

#ifdef USE_IO_REGISTERS
static void ClearBitIO(BYTE reg, int bit)
// Opcodes: 1
{
    Instruction(OP_CBI, IOREG(reg), bit);
}
#endif
//-----------------------------------------------------------------------------
// Configure AVR 16-bit Timer1 to do the timing for us.
//-----------------------------------------------------------------------------
static void ConfigureTimer1(int cycleTimeMicroseconds)
{
    int divisor = 1;
    int countsPerCycle;
    while(divisor <= 1024) {
        int timerRate = (Prog.mcuClock / divisor); // hertz
        double timerPeriod = 1e6 / timerRate; // timer period, us
        countsPerCycle = ((int)(cycleTimeMicroseconds / timerPeriod)) - 1;

        if(countsPerCycle < 1000) {
            Error(_("Cycle time too fast; increase cycle time, or use faster "
                "crystal."));
            CompileError();
        } else if(countsPerCycle > 0xffff) {
            if(divisor >= 1024) {
                Error(
                    _("Cycle time too slow; decrease cycle time, or use slower "
                    "crystal."));
                CompileError();
            }
        } else {
            break;
        }

        if(divisor == 1) divisor = 8;
        else if(divisor == 8) divisor = 64;
        else if(divisor == 64) divisor = 256;
        else if(divisor == 256) divisor = 1024;
    }
    WriteMemory(REG_TCCR1A, 0x00); // WGM11=0, WGM10=0

    int csn;
    switch(divisor) {
        case    1: csn = 1; break;
        case    8: csn = 2; break;
        case   64: csn = 3; break;
        case  256: csn = 4; break;
        case 1024: csn = 5; break;
        default: oops();
    }

    WriteMemory(REG_TCCR1B, (1<<3) | csn); // WGM13=0, WGM12=1

    // `the high byte must be written before the low byte'
    WriteMemory(REG_OCR1AH, (countsPerCycle - 1) >> 8);
    WriteMemory(REG_OCR1AL, (countsPerCycle - 1) & 0xff);

    /*
    Bug .. no interupt for timer1 need..

    // Okay, so many AVRs have a register called TIFR, but the meaning of
    // the bits in that register varies from device to device...
    if(strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega162 40-PDIP")==0) {
        WriteMemory(REG_TIMSK, (1 << 6));
    } else {
        WriteMemory(REG_TIMSK, (1 << 4));
    }
    */
}

//-----------------------------------------------------------------------------
// Write the basic runtime. We set up our reset vector, configure all the
// I/O pins, then set up the timer that does the cycling. Next instruction
// written after calling WriteRuntime should be first instruction of the
// timer loop (i.e. the PLC logic cycle).
//-----------------------------------------------------------------------------
static void WriteRuntime(void)
{
    DWORD resetVector = AllocFwdAddr();

    int i;
    Instruction(OP_RJMP, resetVector, 0);       // $0000, RESET
    for(i = 0; i < 34; i++)
        Instruction(OP_RETI, 0, 0);

    FwdAddrIsNow(resetVector);
    Comment("Interrupt table end.");
    Comment("It is ResetVector");

    // set up the stack, which we use only when we jump to multiply/divide
    // routine
    WORD topOfMemory = (WORD)Prog.mcu->ram[0].start + Prog.mcu->ram[0].len - 1;
    WriteMemory(REG_SPH, topOfMemory >> 8);
    WriteMemory(REG_SPL, topOfMemory & 0xff);

    // zero out the memory used for timers, internal relays, etc.
    LoadXAddr(Prog.mcu->ram[0].start + Prog.mcu->ram[0].len);
    Instruction(OP_LDI, 16, 0);
    Instruction(OP_LDI, 18, (Prog.mcu->ram[0].len) & 0xff);
    Instruction(OP_LDI, 19, (Prog.mcu->ram[0].len) >> 8);

    DWORD loopZero = AvrProgWriteP;
    Instruction(OP_SUBI, 26, 1);
    Instruction(OP_SBCI, 27, 0);
    Instruction(OP_ST_X, 16, 0);
    Instruction(OP_SUBI, 18, 1);
    Instruction(OP_SBCI, 19, 0);
    Instruction(OP_TST, 18, 0);
    Instruction(OP_BRNE, loopZero, 0);
    Instruction(OP_TST, 19, 0);
    Instruction(OP_BRNE, loopZero, 0);


    Comment("Set up I/O pins");
    BYTE isInput[MAX_IO_PORTS], isOutput[MAX_IO_PORTS];
    BuildDirectionRegisters(isInput, isOutput);

    if(UartFunctionUsed()) {
        if(Prog.baudRate == 0) {
            Error(_("Zero baud rate not possible."));
            return;
        }

        // bps = Fosc/(16*(X+1))
        // bps*16*(X + 1) = Fosc
        // X = Fosc/(bps*16)-1
        // and round, don't truncate
        int divisor = (Prog.mcuClock + Prog.baudRate*8)/(Prog.baudRate*16) - 1;

        double actual = Prog.mcuClock/(16.0*(divisor+1));
        double percentErr = 100.0*(actual - Prog.baudRate)/Prog.baudRate;

        if(fabs(percentErr) > 2) {
            ComplainAboutBaudRateError(divisor, actual, percentErr);
        }
        if(divisor > 4095) ComplainAboutBaudRateOverflow();

        WriteMemory(REG_UBRRH, divisor >> 8);
        WriteMemory(REG_UBRRL, divisor & 0xff);
        WriteMemory(REG_UCSRB, (1 << RXEN) | (1 << TXEN));

        for(i = 0; i < Prog.mcu->pinCount; i++) {
            if(Prog.mcu->pinInfo[i].pin == Prog.mcu->uartNeeds.txPin) {
                McuIoPinInfo *iop = &(Prog.mcu->pinInfo[i]);
                isOutput[iop->port - 'A'] |= (1 << iop->bit);
                break;
            }
        }
        if(i == Prog.mcu->pinCount) oops();
    }

    if(PwmFunctionUsed()) {
        Comment("PwmFunctionUsed");
        for(i = 0; i < Prog.mcu->pinCount; i++) {
            if(Prog.mcu->pinInfo[i].pin == Prog.mcu->pwmNeedsPin) {
                McuIoPinInfo *iop = &(Prog.mcu->pinInfo[i]);
                isOutput[iop->port - 'A'] |= (1 << iop->bit);
                break;
            }
        }
        if(i == Prog.mcu->pinCount) oops();
    }

    Comment("Turn on the pull-ups, and drive the outputs low to start");
    for(i = 0; Prog.mcu->dirRegs[i] != 0; i++) {
        if(Prog.mcu->dirRegs[i] == 0xff && Prog.mcu->outputRegs[i] == 0xff) {
            // skip this one, dummy entry for MCUs with I/O ports not
            // starting from A
        } else {
            WriteMemory(Prog.mcu->dirRegs[i], isOutput[i]);
            // turn on the pull-ups, and drive the outputs low to start
            WriteMemory(Prog.mcu->outputRegs[i], isInput[i]);
        }
    }

    Comment("ConfigureTimerForPlcCycle");

    ConfigureTimer1(Prog.cycleTime);

    Comment("and now the generated PLC code will follow");
    Comment("Begin Of PLC Cycle");
    BeginningOfCycleAddr = AvrProgWriteP;


    IfBitClear(REG_TIFR1, OCF1A);
    Instruction(OP_RJMP, BeginningOfCycleAddr); // Ladder cycle timing on Timer1/Counter


    SetBit(REG_TIFR1, OCF1A);
    //To clean a bit in the register TIFR need write 1 in the corresponding bit!

    //Comment("Watchdog reset");
    //Instruction(OP_WDR, 0, 0); // moved to the end of PLC cycle
}

//-----------------------------------------------------------------------------
// Handle an IF statement. Flow continues to the first instruction generated
// by this function if the condition is true, else it jumps to the given
// address (which is an FwdAddress, so not yet assigned). Called with IntPc
// on the IF statement, returns with IntPc on the END IF.
//-----------------------------------------------------------------------------
static void CompileIfBody(DWORD condFalse)
{
    IntPc++;
    IntPcNow = IntPc;
    CompileFromIntermediate();
    if(IntCode[IntPc].op == INT_ELSE) {
        IntPc++;
        IntPcNow = IntPc;
        DWORD endBlock = AllocFwdAddr();
        Instruction(OP_RJMP, endBlock, 0);

        FwdAddrIsNow(condFalse);
        CompileFromIntermediate();
        FwdAddrIsNow(endBlock);
    } else {
        FwdAddrIsNow(condFalse);
    }

    if(IntCode[IntPc].op != INT_END_IF) ooops("INT_%d", IntCode[IntPc].op);
}

//-----------------------------------------------------------------------------
// Call a subroutine, using either an rcall or an icall depending on what
// the processor supports or requires.
//-----------------------------------------------------------------------------
static void CallSubroutine(DWORD addr)
//used ZL
{
    if(Prog.mcu->Family >= ClassicCore8K) {
        Instruction(OP_LDI, ZL, FWD_LO(addr));
        Instruction(OP_LDI, ZH, FWD_HI(addr));
        Instruction(OP_ICALL, 0, 0);
    } else {
        Instruction(OP_RCALL, addr, 0);
    }
}

//-----------------------------------------------------------------------------
static void CopyLiteralToRegs(int reg, int literal, int sov)
{
    Instruction(OP_LDI, reg, (literal & 0xff));
    Instruction(OP_LDI, reg+1, (literal >> 8) & 0xff);
}
//-----------------------------------------------------------------------------
static void CopyVarToRegs(int reg, char *var, int sovRegs)
{
    DWORD addrl, addrh;
    int sov = SizeOfVar(var);
    if(sov != sovRegs)
      dbp("reg=%d sovRegs=%d <- var=%s sov=%d",reg,sovRegs,var,sov);

    MemForVariable(var, &addrl, &addrh);
    LoadXAddr(addrl);

    Instruction(OP_LD_XP, reg);
    if(sovRegs >= 2) {
        if(sov >= 2)
            Instruction(OP_LD_XP, reg+1);
        else {
            Instruction(OP_LDI, reg+1, 0);
            Instruction(OP_SBRC, reg, BIT7);
            Instruction(OP_LDI, reg+1, 0xff);
        }
    }
    if(sovRegs >= 3) {
        if(sov >= 3)
            Instruction(OP_LD_XP, reg+2);
        else {
            Instruction(OP_LDI, reg+2, 0);
            Instruction(OP_SBRC, reg+1, BIT7);
            Instruction(OP_LDI, reg+2, 0xff);
        }
    }
}
//-----------------------------------------------------------------------------
static void CopyRegsToVar(char *var, int reg, int sovRegs)
{
    DWORD addrl, addrh;
    int sov = SizeOfVar(var);
    if(sov != sovRegs)
      dbp("var=%s sov=%d <- reg=%d sovRegs=%d",var,sov,reg,sovRegs);

    MemForVariable(var, &addrl, &addrh);
    LoadXAddr(addrl);

    Instruction(OP_ST_XP, reg);
    if(sov >= 2) {
        if(sovRegs >= 2)
            Instruction(OP_ST_XP, reg+1);
        else {
            Instruction(OP_LDI, reg+1, 0);
            Instruction(OP_SBRC, reg, BIT7);
            Instruction(OP_LDI, reg+1, 0xff);
        }
    }
    if(sov >= 3) {
        if(sovRegs >= 3)
            Instruction(OP_ST_XP, reg+2);
        else {
            Instruction(OP_LDI, reg+2, 0);
            Instruction(OP_SBRC, reg+1, BIT7);
            Instruction(OP_LDI, reg+2, 0xff);
        }
    }
}
// Compile the intermediate code to AVR native code.
//-----------------------------------------------------------------------------
static void CompileFromIntermediate(void)
{
    DWORD addr, addr2;
    int bit, bit2;
    DWORD addrl, addrh;
    DWORD addrl2, addrh2;

    for(; IntPc < IntCodeLen; IntPc++) {
        IntPcNow = IntPc;
        IntOp *a = &IntCode[IntPc];
        rungNow = a->rung;
        switch(a->op) {
            case INT_SET_BIT:
                MemForSingleBit(a->name1, FALSE, &addr, &bit);
                SetBit(addr, bit);
                break;

            case INT_CLEAR_BIT:
                MemForSingleBit(a->name1, FALSE, &addr, &bit);
                ClearBit(addr, bit);
                break;

            case INT_COPY_BIT_TO_BIT:
                MemForSingleBit(a->name1, FALSE, &addr, &bit);
                MemForSingleBit(a->name2, FALSE, &addr2, &bit2);
                CopyBit(addr, bit, addr2, bit2);
                break;

            case INT_SET_VARIABLE_TO_LITERAL:
                MemForVariable(a->name1, &addrl, &addrh);
                WriteMemory(addrl, a->literal & 0xff);
                WriteMemory(addrh, a->literal >> 8);
                break;

            case INT_INCREMENT_VARIABLE: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);
                // increment
                Instruction(OP_INC, 16, 0);
                DWORD noCarry = AllocFwdAddr();
                Instruction(OP_BRNE, noCarry, 0);
                Instruction(OP_INC, 17, 0);
                FwdAddrIsNow(noCarry);
                // X is still addrh
                Instruction(OP_ST_XS, 17, 0);
                //LoadXAddr(addrl);
                Instruction(OP_ST_X, 16, 0);
                break;
            }
            case INT_IF_BIT_SET: {
                DWORD condFalse = AllocFwdAddr();
                MemForSingleBit(a->name1, TRUE, &addr, &bit);
                IfBitClear(addr, bit);
                Instruction(OP_RJMP, condFalse, 0);
                CompileIfBody(condFalse);
                break;
            }
            case INT_IF_BIT_CLEAR: {
                DWORD condFalse = AllocFwdAddr();
                MemForSingleBit(a->name1, TRUE, &addr, &bit);
                IfBitSet(addr, bit);
                Instruction(OP_RJMP, condFalse, 0);
                CompileIfBody(condFalse);
                break;
            }
            case INT_IF_VARIABLE_LES_LITERAL: {
                DWORD notTrue = AllocFwdAddr();

                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);

                Instruction(OP_LDI, 18, (a->literal & 0xff));
                Instruction(OP_LDI, 19, (a->literal >> 8));

                Instruction(OP_CP, 16, 18);
                Instruction(OP_CPC, 17, 19);
                Instruction(OP_BRGE, notTrue, 0);

                CompileIfBody(notTrue);
                break;
            }
            case INT_IF_VARIABLE_GRT_VARIABLE:
            case INT_IF_VARIABLE_EQUALS_VARIABLE: {
                DWORD notTrue = AllocFwdAddr();

                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);
                MemForVariable(a->name2, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 18, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 19, 0);

                if(a->op == INT_IF_VARIABLE_EQUALS_VARIABLE) {
                    Instruction(OP_CP, 16, 18);
                    Instruction(OP_CPC, 17, 19);
                    Instruction(OP_BRNE, notTrue, 0);
                } else if(a->op == INT_IF_VARIABLE_GRT_VARIABLE) {
                    DWORD isTrue = AllocFwdAddr();

                    // true if op1 > op2
                    // false if op1 >= op2
                    Instruction(OP_CP, 18, 16);
                    Instruction(OP_CPC, 19, 17);
                    Instruction(OP_BRGE, notTrue, 0);
                } else oops();
                CompileIfBody(notTrue);
                break;
            }
            // Sepcial function
            case INT_READ_SFR_LITERAL: {
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(addrl);
                Instruction(OP_ST_X, 16, 0);
                break;
            }
            case INT_READ_SFR_VARIABLE: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(addrl2);
                Instruction(OP_ST_X, 16, 0);
                break;
            }
            case INT_WRITE_SFR_LITERAL_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 28, (a->literal2 & 0xff));
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_ST_X, 28, 0);
                break;
            }
            case INT_WRITE_SFR_VARIABLE_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LDI, 28, (a->literal & 0xff));
                Instruction(OP_ST_X, 28, 0);
                break;
            }
            case INT_WRITE_SFR_LITERAL: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 15, 0);
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_ST_X, 15, 0);
                break;
            }
            case INT_WRITE_SFR_VARIABLE: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);
                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 15, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_ST_X, 15, 0);
                break;
            }
            case INT_SET_SFR_LITERAL_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 28, (a->literal2 & 0xff));
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_OR, 1, 28);  // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0);
                break;
            }
            case INT_SET_SFR_VARIABLE_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_LDI, 28, (a->literal & 0xff));
                Instruction(OP_OR, 1, 28);  // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0);
                break;
            }
            case INT_SET_SFR_LITERAL: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_OR, 1, 0);   // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0); // store R1 back to SFR
                break;
            }
            case INT_SET_SFR_VARIABLE: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_OR, 1, 0);   // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0); // store R1 back to SFR
                break;
            }
            case INT_CLEAR_SFR_LITERAL_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 28, (a->literal2 & 0xff));
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 0, 0);  //
                Instruction(OP_AND, 1, 28); //
                Instruction(OP_ST_X, 1, 0);
                break;
            }
            case INT_CLEAR_SFR_VARIABLE_L: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_LDI, 28, (a->literal & 0xff));
                Instruction(OP_COM, 0, 0);  //
                Instruction(OP_AND, 1, 28); //
                Instruction(OP_ST_X, 1, 0);
                break;
            }
            case INT_CLEAR_SFR_LITERAL: {
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 0, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0); // store R1 back to SFR
                break;
            }
            case INT_CLEAR_SFR_VARIABLE: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 0, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_ST_X, 1, 0); // store R1 back to SFR
                break;
            }
            case INT_TEST_SFR_LITERAL_L: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 28, (a->literal2 & 0xff));
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_AND, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_SFR_VARIABLE_L: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_LDI, 28, (a->literal & 0xff));
                Instruction(OP_AND, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_SFR_LITERAL: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_SFR_VARIABLE: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_C_SFR_LITERAL_L: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                Instruction(OP_LDI, 28, (a->literal2 & 0xff));
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_C_SFR_VARIABLE_L: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_LDI, 28, (a->literal & 0xff));
                Instruction(OP_COM, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 28); // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_C_SFR_LITERAL: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                Instruction(OP_LDI, 26, (a->literal & 0xff));
                Instruction(OP_LDI, 27, (a->literal >> 8));
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_TEST_C_SFR_VARIABLE: {
                DWORD notTrue = AllocFwdAddr();
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);
                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 0, 0); // read byte from variable to r0
                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                Instruction(OP_LD_XP, 17, 0);
                Instruction(OP_MOV, 26, 16);
                Instruction(OP_MOV, 27, 17);
                Instruction(OP_LD_X, 1, 0); // read byte from SFR
                Instruction(OP_COM, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_AND, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_EOR, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_TST, 1, 0);  // logic OR by R1,R0 result is in R1
                Instruction(OP_BRNE, notTrue, 0);
                CompileIfBody(notTrue);
                break;
            }
            case INT_SET_VARIABLE_TO_VARIABLE:
                MemForVariable(a->name1, &addrl, &addrh);
                MemForVariable(a->name2, &addrl2, &addrh2);

                LoadXAddr(addrl2);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(addrl);
                Instruction(OP_ST_X, 16, 0);

                LoadXAddr(addrh2);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(addrh);
                Instruction(OP_ST_X, 16, 0);
                break;

            case INT_SET_VARIABLE_DIVIDE:
                // Do this one separately since the divide routine uses
                // slightly different in/out registers and I don't feel like
                // modifying it.
                MemForVariable(a->name2, &addrl, &addrh);
                MemForVariable(a->name3, &addrl2, &addrh2);

                LoadXAddr(addrl2);
                Instruction(OP_LD_XP, 18, 0);
                //LoadXAddr(addrh2);
                Instruction(OP_LD_X, 19, 0);

                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 17, 0);

                CallSubroutine(DivideAddress);
                DivideUsed = TRUE;

                MemForVariable(a->name1, &addrl, &addrh);

                LoadXAddr(addrl);
                Instruction(OP_ST_XP, 16, 0);
                //LoadXAddr(addrh);
                Instruction(OP_ST_X, 17, 0);
                break;

            case INT_SET_VARIABLE_ADD:
            case INT_SET_VARIABLE_SUBTRACT:
            case INT_SET_VARIABLE_MULTIPLY:
                MemForVariable(a->name2, &addrl, &addrh);
                MemForVariable(a->name3, &addrl2, &addrh2);

                LoadXAddr(addrl);
                Instruction(OP_LD_XP, 18, 0);
                //LoadXAddr(addrh);
                Instruction(OP_LD_X, 19, 0);

                LoadXAddr(addrl2);
                Instruction(OP_LD_XP, 16, 0);
                //LoadXAddr(addrh2);
                Instruction(OP_LD_X, 17, 0);

                if(a->op == INT_SET_VARIABLE_ADD) {
                    Instruction(OP_ADD, 18, 16);
                    Instruction(OP_ADC, 19, 17);
                } else if(a->op == INT_SET_VARIABLE_SUBTRACT) {
                    Instruction(OP_SUB, 18, 16);
                    Instruction(OP_SBC, 19, 17);
                } else if(a->op == INT_SET_VARIABLE_MULTIPLY) {
                    CallSubroutine(MultiplyAddress);
                    MultiplyUsed = TRUE;
                } else oops();

                MemForVariable(a->name1, &addrl, &addrh);

                LoadXAddr(addrl);
                Instruction(OP_ST_XP, 18, 0);
                //LoadXAddr(addrh);
                Instruction(OP_ST_X, 19, 0);
                break;

            case INT_SET_PWM: {
                int target = atoi(a->name2);

                // PWM frequency is
                //   target = xtal/(256*prescaler)
                // so not a lot of room for accurate frequency here

                double freq;
                double bestFreq;
                int err;
                int bestPrescaler;
                int bestError = INT_MAX;
                int prescaler;
                for(prescaler = 1;;) {
                    //int freq = (Prog.mcuClock + prescaler*128)/(prescaler*256);
                    freq = (double)Prog.mcuClock/(prescaler*256);
                    //dbp("prescaler=%d freq=%f", prescaler, freq);

                    err = abs(freq - target);
                    if(err < bestError) {
                        bestError = err;
                        bestPrescaler = prescaler;
                        bestFreq = freq;
                    }

                    if(prescaler == 1) prescaler = 8;
                    else if(prescaler == 8) prescaler = 32;
                    else if(prescaler == 32) prescaler = 64;
                    else if(prescaler == 64) prescaler = 128;
                    else if(prescaler == 128) prescaler = 256;
                    else if(prescaler == 256) prescaler = 1024;
                    else break;
                }
                //dbp("bestPrescaler=%d bestFreq=%f", bestPrescaler, bestFreq);

                if(((double)bestError)/target > 0.05)
                    Error(_(" Target PWM frequency %.3f Hz, closest achievable is "
                        "%.3f Hz (warning, >5%% error)."), target, bestFreq);

                BOOL Use100 = TRUE;
                // Use100 = FALSE; // TODO
                if(Use100) {

                DivideUsed = TRUE; MultiplyUsed = TRUE;
                    CopyVarToRegs(r20, a->name1, SizeOfVar(a->name1));
                    CopyLiteralToRegs(r16, 255, SizeOfVar(a->name1));
                CallSubroutine(MultiplyAddress);

                    Instruction(OP_MOV, 19, 20);
                    Instruction(OP_MOV, 20, 21);
                    Instruction(OP_MOV, 21, 22);

                    CopyLiteralToRegs(r22, 100, SizeOfVar(a->name1));
                CallSubroutine(DivideAddress);
                  //LoadXAddr(REG_OCR2);
                  //Instruction(OP_ST_X, 19);
                    WriteRegToIO(REG_OCR2, r19);
                } else {
                    CopyVarToRegs(r20, a->name1, SizeOfVar(a->name1));
                    WriteRegToIO(REG_OCR2, r20);
                }

                // Setup only happens once
                MemForSingleBit("$pwm_init", FALSE, &addr, &bit);
                DWORD skip = AllocFwdAddr();
                IfBitSet(addr, bit);
                Instruction(OP_RJMP, skip);
                SetBit(addr, bit);

                BYTE cs; // see Timer/Counter2
                switch(bestPrescaler) {
                    case    1: cs = 1; break;
                    case    8: cs = 2; break;
                    case   32: cs = 3; break;
                    case   64: cs = 4; break;
                    case  128: cs = 5; break;
                    case  256: cs = 6; break;
                    case 1024: cs = 7; break;
                    default: oops(); break;
                }
                // fast PWM mode, non-inverting or inverting mode, given prescaler
                if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega48 ") ||
                   strstr(Prog.mcu->mcuName, "Atmel AVR ATmega88 ") ||
                   strstr(Prog.mcu->mcuName, "Atmel AVR ATmega168 ") ||
                   strstr(Prog.mcu->mcuName, "Atmel AVR ATmega328 ")
                ){
                     WriteMemory(REG_TCCR2B, cs);
                     WriteMemory(REG_TCCR2, (1 << WGM20) | (1 << WGM21) | (1 << COM21) | (a->name2[0]=='/' ? (1 << COM20) : 0));
                } else {
                     //TODO: test registers and bits define's
                     WriteMemory(REG_TCCR2, (1 << WGM20) | (1 << WGM21) | (1 << COM21) | (a->name2[0]=='/' ? (1 << COM20) : 0) | cs);
                }

                FwdAddrIsNow(skip);

                break;
            }
            case INT_EEPROM_BUSY_CHECK: {
                MemForSingleBit(a->name1, FALSE, &addr, &bit);

                DWORD isBusy = AllocFwdAddr();
                DWORD done = AllocFwdAddr();
                IfBitSet(REG_EECR, EEWE);
                Instruction(OP_RJMP, isBusy, 0);

                IfBitClear(EepromHighByteWaitingAddr, EepromHighByteWaitingBit);
                Instruction(OP_RJMP, done, 0);

                // Just increment EEARH:EEARL, to point to the high byte of
                // whatever we just wrote the low byte for.
                LoadXAddr(REG_EEARL);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(REG_EEARH);
                Instruction(OP_LD_X, 17, 0);
                Instruction(OP_INC, 16, 0);
                DWORD noCarry = AllocFwdAddr();
                Instruction(OP_BRNE, noCarry, 0);
                Instruction(OP_INC, 17, 0);
                FwdAddrIsNow(noCarry);
                // X is still REG_EEARH
                Instruction(OP_ST_X, 17, 0);
                LoadXAddr(REG_EEARL);
                Instruction(OP_ST_X, 16, 0);

                LoadXAddr(EepromHighByte);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(REG_EEDR);
                Instruction(OP_ST_X, 16, 0);
                LoadXAddr(REG_EECR);
                Instruction(OP_LDI, 16, (1 << EEMWE)); // 0x04
                Instruction(OP_ST_X, 16, 0);
                Instruction(OP_LDI, 16, (1 << EEMWE) | (1 << EEWE)); // 0x06
                Instruction(OP_ST_X, 16, 0);

                ClearBit(EepromHighByteWaitingAddr, EepromHighByteWaitingBit);

                FwdAddrIsNow(isBusy);
                SetBit(addr, bit);
                FwdAddrIsNow(done);
                break;
            }
            case INT_EEPROM_READ: {
                MemForVariable(a->name1, &addrl, &addrh);
                int i;
                for(i = 0; i < 2; i++) {
                    WriteMemory(REG_EEARH, ((a->literal+i) >> 8) & 0xff);
                    WriteMemory(REG_EEARL, ((a->literal+i) & 0xff));
                    WriteMemory(REG_EECR, 1 << EERE);
                    LoadXAddr(REG_EEDR);
                    Instruction(OP_LD_X, 16, 0);
                    if(i == 0) {
                        LoadXAddr(addrl);
                    } else {
                        LoadXAddr(addrh);
                    }
                    Instruction(OP_ST_X, 16, 0);
                }
                break;
            }
            case INT_EEPROM_WRITE:
                MemForVariable(a->name1, &addrl, &addrh);
                SetBit(EepromHighByteWaitingAddr, EepromHighByteWaitingBit);
                LoadXAddr(addrh);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(EepromHighByte);
                Instruction(OP_ST_X, 16, 0);

                WriteMemory(REG_EEARH, (a->literal >> 8) & 0xff);
                WriteMemory(REG_EEARL, (a->literal & 0xff));
                LoadXAddr(addrl);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(REG_EEDR);
                Instruction(OP_ST_X, 16, 0);
                LoadXAddr(REG_EECR);
                Instruction(OP_LDI, 16, (1 << EEMWE)); // 0x04
                Instruction(OP_ST_X, 16, 0);
                Instruction(OP_LDI, 16, (1 << EEMWE) | (1 << EEWE)); // 0x06
                Instruction(OP_ST_X, 16, 0);
                break;

            case INT_READ_ADC: {
                MemForVariable(a->name1, &addrl, &addrh);

                int mux = MuxForAdcVariable(a->name1);
                if(mux > 0x0F)
                    ooops("mux=0x%x", mux);
                WriteMemory(REG_ADMUX,
                    (0 << 7) |              // AREF, Internal Vref turned off.
                    (0 << 6) |              // AREF, Internal Vref turned off.
                    (0 << 5) |              // result is right adjusted.
                    mux);

                // target something around 200 kHz for the ADC clock, for
                // 25/(200k) or 125 us conversion time, reasonable
                int divisor = (Prog.mcuClock / 200000);
                int adps = 0;
                for(adps = 1; adps <= 7; adps++) {
                    if((1 << adps) > divisor) break;
                }

                BYTE adcsra =
                    (1 << ADEN) |           // ADC enabled
                    (0 << ADFR) |           // not free running
                    (0 << ADIE) |           // no interrupt enabled
                    adps;                   // prescaler setup

                WriteMemory(REG_ADCSRA, adcsra);
                WriteMemory(REG_ADCSRA, (BYTE)(adcsra | (1 << ADSC)));

                DWORD waitForFinsh = AvrProgWriteP;
                IfBitSet(REG_ADCSRA, ADSC);
                Instruction(OP_RJMP, waitForFinsh);

                LoadXAddr(REG_ADCL);
                Instruction(OP_LD_X, 16);
                LoadYAddr(addrl);
                Instruction(OP_ST_YP, 16);

                LoadXAddr(REG_ADCH);
                Instruction(OP_LD_X, 16);
              //LoadXAddr(addrh);
                Instruction(OP_ST_YP, 16);

                break;
            }
            case INT_UART_SEND_BUSY: {
                MemForSingleBit(a->name1, TRUE, &addr, &bit);
                ClearBit(addr, bit); // UART ready
                DWORD dontSet = AllocFwdAddr();
                IfBitSet(REG_UCSRA, UDRE); // UDRE, is 1 when tx buffer is empty
                Instruction(OP_RJMP, dontSet, 0);
                SetBit(addr, bit); // Set UART busy
                FwdAddrIsNow(dontSet);

                break;
            }
            case INT_UART_SEND: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForSingleBit(a->name2, TRUE, &addr, &bit);

                DWORD noSend = AllocFwdAddr();
                IfBitClear(addr, bit);
                Instruction(OP_RJMP, noSend, 0);

                LoadXAddr(addrl);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(REG_UDR);
                Instruction(OP_ST_X, 16, 0);

                FwdAddrIsNow(noSend);

                ClearBit(addr, bit);
                DWORD dontSet = AllocFwdAddr();
                IfBitSet(REG_UCSRA, 5); // UDRE, is 1 when tx buffer is empty
                Instruction(OP_RJMP, dontSet, 0);
                SetBit(addr, bit); // Set UART busy
                FwdAddrIsNow(dontSet);

                break;
            }
            case INT_UART_RECV_AVAIL: {
                MemForSingleBit(a->name1, TRUE, &addr, &bit);
                SetBit(addr, bit); // Set // TODO
                break;
            }
            case INT_UART_RECV: {
                MemForVariable(a->name1, &addrl, &addrh);
                MemForSingleBit(a->name2, TRUE, &addr, &bit);

                ClearBit(addr, bit);

                DWORD noChar = AllocFwdAddr();
                IfBitClear(REG_UCSRA, 7);
                Instruction(OP_RJMP, noChar, 0);

                SetBit(addr, bit);
                LoadXAddr(REG_UDR);
                Instruction(OP_LD_X, 16, 0);
                LoadXAddr(addrl);
                Instruction(OP_ST_X, 16, 0);

                LoadXAddr(addrh);
                Instruction(OP_LDI, 16, 0);
                Instruction(OP_ST_X, 16, 0);

                FwdAddrIsNow(noChar);
                break;
            }
            case INT_END_IF:
            case INT_ELSE:
                return;

            case INT_WRITE_STRING:
                Error(_("Unsupported operation 'INT_WRITE_STRING' for target, skipped."));
            case INT_SIMULATE_NODE_STATE:
                break;

            case INT_COMMENT:
                if(strlen(a->name1))
                    Instruction(OP_COMMENT, a->name1);
                break;
                break;

            default:
                ooops("INT_%d", a->op);
                break;
        }
    }
}

//-----------------------------------------------------------------------------
// 16x16 signed multiply, code from Atmel app note AVR200. op1 in r17:16,
// op2 in r19:18, result low word goes into r19:18.
//-----------------------------------------------------------------------------
static void MultiplyRoutine(void)
{
    Comment("MultiplyRoutine16");
    FwdAddrIsNow(MultiplyAddress);

    DWORD m16s_1;
    DWORD m16s_2 = AllocFwdAddr();

    Instruction(OP_SUB, 21, 21);
    Instruction(OP_SUB, 20, 20);
    Instruction(OP_LDI, 22, 16);
    m16s_1 = AvrProgWriteP; Instruction(OP_BRCC, m16s_2, 0);
    Instruction(OP_ADD, 20, 16);
    Instruction(OP_ADC, 21, 17);
    FwdAddrIsNow(m16s_2); Instruction(OP_SBRC, 18, 0);
    Instruction(OP_SUB, 20, 16);
    Instruction(OP_SBRC, 18, 0);
    Instruction(OP_SBC, 21, 17);
    Instruction(OP_ASR, 21, 0);
    Instruction(OP_ROR, 20, 0);
    Instruction(OP_ROR, 19, 0);
    Instruction(OP_ROR, 18, 0);
    Instruction(OP_DEC, 22, 0);
    Instruction(OP_BRNE, m16s_1, 0);
    Instruction(OP_RET, 0, 0);
}

//-----------------------------------------------------------------------------
// 16/16 signed divide, code from the same app note. Dividend in r17:16,
// divisor in r19:18, result goes in r17:16 (and remainder in r15:14).
//-----------------------------------------------------------------------------
static void DivideRoutine(void)
{
    Comment("DivideRoutine16");
    FwdAddrIsNow(DivideAddress);

    DWORD d16s_1 = AllocFwdAddr();
    DWORD d16s_2 = AllocFwdAddr();
    DWORD d16s_3;
    DWORD d16s_4 = AllocFwdAddr();
    DWORD d16s_5 = AllocFwdAddr();
    DWORD d16s_6 = AllocFwdAddr();

    Instruction(OP_MOV, 13, 17);
    Instruction(OP_EOR, 13, 19);
    Instruction(OP_SBRS, 17, 7);
    Instruction(OP_RJMP, d16s_1, 0);
    Instruction(OP_COM, 17, 0);
    Instruction(OP_COM, 16, 0);
    Instruction(OP_SUBI, 16, 0xff);
    Instruction(OP_SBCI, 17, 0xff);
    FwdAddrIsNow(d16s_1); Instruction(OP_SBRS, 19, 7);
    Instruction(OP_RJMP, d16s_2, 0);
    Instruction(OP_COM, 19, 0);
    Instruction(OP_COM, 18, 0);
    Instruction(OP_SUBI, 18, 0xff);
    Instruction(OP_SBCI, 19, 0xff);
    FwdAddrIsNow(d16s_2); Instruction(OP_EOR, 14, 14);
    Instruction(OP_SUB, 15, 15);
    Instruction(OP_LDI, 20, 17);

    d16s_3 = AvrProgWriteP; Instruction(OP_ADC, 16, 16);
    Instruction(OP_ADC, 17, 17);
    Instruction(OP_DEC, 20, 0);
    Instruction(OP_BRNE, d16s_5, 0);
    Instruction(OP_SBRS, 13, 7);
    Instruction(OP_RJMP, d16s_4, 0);
    Instruction(OP_COM, 17, 0);
    Instruction(OP_COM, 16, 0);
    Instruction(OP_SUBI, 16, 0xff);
    Instruction(OP_SBCI, 17, 0xff);
    FwdAddrIsNow(d16s_4); Instruction(OP_RET, 0, 0);
    FwdAddrIsNow(d16s_5); Instruction(OP_ADC, 14, 14);
    Instruction(OP_ADC, 15, 15);
    Instruction(OP_SUB, 14, 18);
    Instruction(OP_SBC, 15, 19);
    Instruction(OP_BRCC, d16s_6, 0);
    Instruction(OP_ADD, 14, 18);
    Instruction(OP_ADC, 15, 19);
    Instruction(OP_CLC, 0, 0);
    Instruction(OP_RJMP, d16s_3, 0);
    FwdAddrIsNow(d16s_6); Instruction(OP_SEC, 0, 0);
    Instruction(OP_RJMP, d16s_3, 0);
}

//-----------------------------------------------------------------------------
// Compile the program to REG code for the currently selected processor
// and write it to the given file. Produce an error message if we cannot
// write to the file, or if there is something inconsistent about the
// program.
//-----------------------------------------------------------------------------
void CompileAvr(char *outFile)
{
    rungNow = -100;
    FILE *f = fopen(outFile, "w");
    if(!f) {
        Error(_("Couldn't open file '%s'"), outFile);
        return;
    }

    if(setjmp(CompileErrorBuf) != 0) {
        fclose(f);
        return;
    }

  //REG_MCUCR = 0x55;
        ISC11 = BIT3;
        ISC10 = BIT2;
        ISC01 = BIT1;
        ISC00 = BIT0;

  //REG_GICR  = 0x5B;
        INT1  = BIT7;
        INT0  = BIT6;

  //REG_GIFR  = 0x5A;
        INTF1 = BIT7;
        INTF0 = BIT6;

    //***********************************************************************
    // Interrupt Vectors Table
    if(strstr(Prog.mcu->mcuName, " AT90USB646 ")
    || strstr(Prog.mcu->mcuName, " AT90USB647 ")
    || strstr(Prog.mcu->mcuName, " AT90USB1286 ")
    || strstr(Prog.mcu->mcuName, " AT90USB1287 ")
    ){
        Int0Addr        = 1;
        Int1Addr        = 2;
        Timer1OvfAddr   = 20;
        Timer0OvfAddr   = 23;
        Timer1CompAAddr = 17;
    } else
    if(strstr(Prog.mcu->mcuName, " AT90USB82 ")
    || strstr(Prog.mcu->mcuName, " AT90USB162 ")
    ){
        Int0Addr        = 1;
        Int1Addr        = 2;
        Timer1OvfAddr   = 18;
        Timer0OvfAddr   = 21;
        Timer1CompAAddr = 15;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega161 ")
    || strstr(Prog.mcu->mcuName, " ATmega162 ")
    || strstr(Prog.mcu->mcuName, " ATmega32 ")
    || strstr(Prog.mcu->mcuName, " ATmega323 ")
    ){
        Int0Addr        = 2;
        Int1Addr        = 4;
        Timer1OvfAddr   = 0x0012;
        Timer0OvfAddr   = 0x0016;
        Timer1CompAAddr = 0x000E;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega103 ")
    ){
        Int0Addr        = 1;
        Int1Addr        = 2;
        Timer1OvfAddr   = 14;
        Timer0OvfAddr   = 16;
        Timer1CompAAddr = 12;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega16 ")
    || strstr(Prog.mcu->mcuName, " ATmega163 ")
    ){
        Int0Addr        = 2;
        Int1Addr        = 4;
        Timer1OvfAddr   = 0x0010;
        Timer0OvfAddr   = 0x0012;
        Timer1CompAAddr = 0x000C;
    } else
    if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega48 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega88 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega168 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega328 ")
    ){
        REG_GICR  = 0x3D; // EIMSK
            INT1  = BIT1;
            INT0  = BIT0;

        REG_GIFR  = 0x3C; // EIFR
            INTF1 = BIT1;
            INTF0 = BIT0;

        REG_OCR2   = 0xB3; // OCR2A
        REG_TCCR2  = 0xB0; // TCCR2A
            WGM20  = BIT0;
            WGM21  = BIT1;
            COM21  = BIT7; // COM2A1
            COM20  = BIT6; // COM2A0
        REG_TCCR2B = 0xB1;

        REG_ADMUX  = 0x7C;
        REG_ADCSRA = 0x7A;
        REG_ADCH   = 0x79;
        REG_ADCL   = 0x78;

        REG_EEARH  = 0x42;
        REG_EEARL  = 0x41;
        REG_EEDR   = 0x40;
        REG_EECR   = 0x3F;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega64 ")
    || strstr(Prog.mcu->mcuName, " ATmega128 ")
    ){
        Int0Addr        = 2;
        Int1Addr        = 4;
        Timer1OvfAddr   = 0x001C;
        Timer0OvfAddr   = 0x0020;
        Timer1CompAAddr = 0x0018;
        REG_GICR  = 0x59; // EIMSK
            INT1  = BIT1;
            INT0  = BIT0;

        REG_GIFR  = 0x58; // EIFR
            INTF1 = BIT1;
            INTF0 = BIT0;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega8515 ")
    ){
        Int0Addr        = 1;
        Int1Addr        = 2;
        Timer1OvfAddr   = 0x0006;
        Timer0OvfAddr   = 0x0007;
        Timer1CompAAddr = 0x0004;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega8 ")
    || strstr(Prog.mcu->mcuName, " ATmega8535 ")
    ){
        Int0Addr        = 1;
        Int1Addr        = 2;
        Timer1OvfAddr   = 0x0008;
        Timer0OvfAddr   = 0x0009;
        Timer1CompAAddr = 0x0006;
    }; // else oops();

    //***********************************************************************
    // Okay, so many AVRs have a register called TIFR, but the meaning of
    // the bits in that register varies from device to device...

    // TODO: move bits down to regs defs
    if(strstr(Prog.mcu->mcuName, " AT90USB82 ")
    || strstr(Prog.mcu->mcuName, " AT90USB162 ")
    ){//TIFR bits
        OCF1A  = BIT1;
        TOV0   = BIT0;
      //TIFR1  bits
        TOV1   = BIT0;
      //TIMSK  bits
        OCIE1A = BIT1;
        TOIE1  = BIT0;
        TOIE0  = BIT0;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega103 ")
    || strstr(Prog.mcu->mcuName, " ATmega163 ")
    || strstr(Prog.mcu->mcuName, " ATmega323 ")
    || strstr(Prog.mcu->mcuName, " ATmega8535 ")
    ){//TIFR bits
        OCF1A  = BIT4;
        TOV1   = BIT2;
        TOV0   = BIT0;
      //TIMSK bits
        OCIE1A = BIT4;
        TOIE1  = BIT2;
        TOIE0  = BIT0;
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega161 ")
    || strstr(Prog.mcu->mcuName, "Atmel AVR ATmega162 ")
    || strstr(Prog.mcu->mcuName, " ATmega8515 ")
    ){//TIFR bits
        TOV1   = BIT7;
        OCF1A  = BIT6;
        TOV0   = BIT1;
      //TIMSK  bits
        TOIE1  = BIT7;
        OCIE1A = BIT6;
        TOIE0  = BIT1;
    } else
    if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega16 ")
    || strstr(Prog.mcu->mcuName, "Atmel AVR ATmega32 ")
    || strstr(Prog.mcu->mcuName, "Atmel AVR ATmega8 ")
    ){//TIFR bits
        OCF1A  = BIT4;
        TOV1   = BIT2;
        TOV0   = BIT0;
      //TIMSK bits
        OCIE1A = BIT4;
        TOIE1  = BIT2;
        TOIE0  = BIT0;
    }; // else oops();

    //***********************************************************************
    // Here we must set up the addresses of some registers that for some
    // stupid reason move around from AVR to AVR.
    if(strstr(Prog.mcu->mcuName, " AT90USB646 ")
    || strstr(Prog.mcu->mcuName, " AT90USB647 ")
    || strstr(Prog.mcu->mcuName, " AT90USB1286 ")
    || strstr(Prog.mcu->mcuName, " AT90USB1287 ")
    ){
        REG_OCR1AH  = 0x89;
        REG_OCR1AL  = 0x88;
        REG_TCCR1A  = 0x80;
        REG_TCCR1B  = 0x81;

//      REG_TIMSK   = 0x6F;
            OCIE1A  = BIT1;
            TOIE1   = BIT0;
            TOIE0   = BIT0;
        REG_TIFR1   = 0x36;
            OCF1A   = BIT1;
            TOV1    = BIT0;
        REG_TIFR0   = 0x35;
            TOV0    = BIT0;
        REG_UDR     = 0xCE;
        REG_UBRRH   = 0xCD;
        REG_UBRRL   = 0xCC;
        REG_UCSRC   = 0xCA;
        REG_UCSRB   = 0xC9;
        REG_UCSRA   = 0xC8;
    } else
    if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega48 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega88 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega168 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega328 ")
    ){
        REG_OCR1AH  = 0x89;
        REG_OCR1AL  = 0x88;
        REG_TCCR1A  = 0x80;
        REG_TCCR1B  = 0x81;

//      REG_TIMSK   = 0x6F;   // TIMSK1
        REG_TIFR1   = 0x36;
            OCF1A   = BIT1;
            TOV1    = BIT0;
        REG_TIFR0   = 0x35;
            TOV0    = BIT0;

        REG_UDR     = 0xC6;   // UDR0
        REG_UBRRH   = 0xC5;   // UBRR0H
        REG_UBRRL   = 0xC4;   // UBRR0L
        REG_UCSRC   = 0xC2;   // UCSR0C
        REG_UCSRB   = 0xC1;   // UCSR0B
        REG_UCSRA   = 0xC0;   // UCSR0A
    } else
    if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega164 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega324 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega644 ")
    ){
        REG_OCR1AH  = 0x89;
        REG_OCR1AL  = 0x88;
        REG_TCCR1B  = 0x81;
        REG_TCCR1A  = 0x80;

//      REG_TIMSK   = 0x6F;   // TIMSK1
        REG_TIFR1   = 0x36;
            OCF1A   = BIT1;
            TOV1    = BIT0;
        REG_TIFR0   = 0x35;
            TOV0    = BIT0;

        REG_UDR     = 0xC6;   // UDR0
        REG_UBRRH   = 0xC5;   // UBRR0H
        REG_UBRRL   = 0xC4;   // UBRR0L
        REG_UCSRC   = 0xC2;   // UCSR0C
        REG_UCSRB   = 0xC1;   // UCSR0B
        REG_UCSRA   = 0xC0;   // UCSR0A
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega640 ") ||
       strstr(Prog.mcu->mcuName, " ATmega1280 ") ||
       strstr(Prog.mcu->mcuName, " ATmega1281 ") ||
       strstr(Prog.mcu->mcuName, " ATmega2560 ") ||
       strstr(Prog.mcu->mcuName, " ATmega2561 ")
    ){
        REG_OCR1AH  = 0x89;
        REG_OCR1AL  = 0x88;
        REG_TCCR1B  = 0x81;
        REG_TCCR1A  = 0x80;

//      REG_TIMSK   = 0x6F;   // TIMSK1
        REG_TIFR1   = 0x36;
            OCF1A   = BIT1;
            TOV1    = BIT0;
        REG_TIFR0   = 0x35;
            TOV0    = BIT0;

        REG_UDR     = 0xC6;   // UDR0
        REG_UBRRH   = 0xC5;   // UBRR0H
        REG_UBRRL   = 0xC4;   // UBRR0L
        REG_UCSRC   = 0xC2;   // UCSR0C
        REG_UCSRB   = 0xC1;   // UCSR0B
        REG_UCSRA   = 0xC0;   // UCSR0A
    } else
    if(strstr(Prog.mcu->mcuName, " ATmega164 ") ||
       strstr(Prog.mcu->mcuName, " ATmega324 ") ||
       strstr(Prog.mcu->mcuName, " ATmega644 ") ||
       strstr(Prog.mcu->mcuName, " ATmega1284 ")
    ){
        REG_OCR1AH  = 0x89;
        REG_OCR1AL  = 0x88;
        REG_TCCR1B  = 0x81;
        REG_TCCR1A  = 0x80;

//      REG_TIMSK   = 0x6F;   // TIMSK1
        REG_TIFR1   = 0x36;
            OCF1A   = BIT1;
            TOV1    = BIT0;
        REG_TIFR0   = 0x35;
            TOV0    = BIT0;

        REG_UDR     = 0xC6;   // UDR0
        REG_UBRRH   = 0xC5;   // UBRR0H
        REG_UBRRL   = 0xC4;   // UBRR0L
        REG_UCSRC   = 0xC2;   // UCSR0C
        REG_UCSRB   = 0xC1;   // UCSR0B
        REG_UCSRA   = 0xC0;   // UCSR0A
    } else
    if(strstr(Prog.mcu->mcuName,  "Atmel AVR ATmega103 ")
    ){
        REG_OCR1AH  = 0x4B;
        REG_OCR1AL  = 0x4A;
        REG_TCCR1A  = 0x4F;
        REG_TCCR1B  = 0x4E;

//      REG_TIMSK = 0x57;
        REG_TIFR1 = 0x56; // TIFR
        REG_TIFR0 = 0x56; // TIFR
//      REG_UBRRH = 0x98;
        REG_UBRRL = 0x29; // UBRR
        REG_UCSRB = 0x2a; // UCR
        REG_UCSRA = 0x2b; // USR
        REG_UDR   = 0x2c;
//      REG_UCSRC = 0x9d;
    } else
    if(strstr(Prog.mcu->mcuName, "Atmel AVR ATmega16 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega32 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega162 ") ||
       strstr(Prog.mcu->mcuName, "Atmel AVR ATmega8 ")
    ){
        REG_OCR1AH  = 0x4B;
        REG_OCR1AL  = 0x4A;
        REG_TCCR1A  = 0x4F;
        REG_TCCR1B  = 0x4E;

//      REG_TIMSK = 0x59;
        REG_TIFR1 = 0x58;
        REG_TIFR0 = 0x58;
        REG_UCSRC = 0x40;
        REG_UBRRH = 0x40;
        REG_UBRRL = 0x29;
        REG_UCSRB = 0x2a;
        REG_UCSRA = 0x2b;
        REG_UDR   = 0x2c;
    } else
    if(strstr(Prog.mcu->mcuName,  "Atmel AVR ATmega64 ") ||
       strstr(Prog.mcu->mcuName,  "Atmel AVR ATmega128 ")
    ){
        REG_OCR1AH  = 0x4B;
        REG_OCR1AL  = 0x4A;
        REG_TCCR1A  = 0x4F;
        REG_TCCR1B  = 0x4E;

//      REG_TIMSK   = 0x57;
            OCIE1A  = BIT4;
            TOIE1   = BIT2;
            TOIE0   = BIT0;
        REG_TIFR1   = 0x56; // TIFR
        REG_TIFR0   = 0x56; // TIFR
            OCF1A   = BIT4;
            TOV1    = BIT2;
            TOV0    = BIT0;
        REG_UBRRH   = 0x98; // UBRR1H
        REG_UBRRL   = 0x99; // UBRR1L
        REG_UCSRB   = 0x9a; // UCSR1B
        REG_UCSRA   = 0x9b; // UCSR1A
        REG_UDR     = 0x9c; // UDR1
        REG_UCSRC   = 0x9d; // UCSR1C
    } else oops();
    //***********************************************************************
/*
    if(strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega16 40-PDIP")==0  ||
       strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega32 40-PDIP")==0 ||
       strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega162 40-PDIP")==0 ||
       strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega8 28-PDIP")==0)
    {
    } else {
    if(strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega128 64-TQFP")==0 ||
       strcmp(Prog.mcu->mcuName, "Atmel AVR ATmega64 64-TQFP")==0)
    {
    }
*/
    //***********************************************************************

    rungNow = -90;
    WipeMemory();
    MultiplyUsed = FALSE;
    MultiplyAddress = AllocFwdAddr();
    DivideUsed = FALSE;
    DivideAddress = AllocFwdAddr();
    rungNow = -80;
    AllocStart();

    rungNow = -70;
    if(EepromFunctionUsed()) {
        // Where we hold the high byte to program in EEPROM while the low byte
        // programs.
        EepromHighByte = AllocOctetRam();
        AllocBitRam(&EepromHighByteWaitingAddr, &EepromHighByteWaitingBit);
    }

    rungNow = -60;
    WriteRuntime();

    Comment("CompileFromIntermediate");
    IntPc = 0; // Ok
    CompileFromIntermediate();

    DWORD i;
    for(i = 0; i < MAX_RUNGS; i++)
        Prog.HexInRung[i] = 0;
    for(i = 0; i < AvrProgWriteP; i++)
        if((AvrProg[i].rung >= 0)
        && (AvrProg[i].rung < MAX_RUNGS))
            Prog.HexInRung[AvrProg[i].rung]++;

    rungNow = -40;
    Comment("Watchdog reset");
    Instruction(OP_WDR);

    rungNow = -30;
    Comment("GOTO next PLC cycle");
    //GOTO next PLC cycle
    if(Prog.mcu->Family >= ClassicCore8K) {
        Instruction(OP_LDI, ZL, (BeginningOfCycleAddr & 0xff));
        Instruction(OP_LDI, ZH, (BeginningOfCycleAddr >> 8) & 0xff);
        Instruction(OP_IJMP, 0, 0);
    } else {
        Instruction(OP_RJMP, BeginningOfCycleAddr, 0);
    }
    //Instruction(OP_RJMP, AvrProgWriteP); // as CodeVisionAVR C Compiler


    if(MultiplyUsed) MultiplyRoutine();
    if(DivideUsed) DivideRoutine();

    rungNow = -10;
    MemCheckForErrorsPostCompile();
    AddrCheckForErrorsPostCompile();

    ProgWriteP = AvrProgWriteP;
    WriteHexFile(f);
    fclose(f);

    char str[MAX_PATH+500];
    sprintf(str, _("Compile successful; wrote IHEX for AVR to '%s'.\r\n\r\n"
        "Remember to set the processor configuration (fuses) correctly. "
        "This does not happen automatically."), outFile);
    CompileSuccessfulMessage(str);
}
