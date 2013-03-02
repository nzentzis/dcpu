#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <list>

struct DCPUState;

enum DCPUOpcode {
	// Basic instruction set
	DO_SET=0, DO_ADD, DO_SUB, DO_MUL, DO_MLI, DO_DIV, DO_DVI, DO_MOD, DO_MDI,
	DO_AND, DO_BOR, DO_XOR, DO_SHR, DO_ASR, DO_SHL, DO_IFB, DO_IFC, DO_IFE,
	DO_IFN, DO_IFG, DO_IFA, DO_IFL, DO_IFU, DO_ADX, DO_SBX, DO_STI, DO_STD,
	
	// Special instructions
	DO_JSR, DO_INT, DO_IAG, DO_IAS, DO_RFI, DO_IAQ, DO_HWN, DO_HWQ, DO_HWI,
	
	// Other
	DO_INVALID
};

struct DCPUValue {
	enum ValueType {
		VT_REGISTER, VT_INDIRECT_REGISTER, VT_INDIRECT_REGISTER_OFFSET,
		VT_PUSHPOP, VT_PEEK, VT_PICK, VT_SP, VT_PC, VT_EX, VT_MEMORY,
		VT_LITERAL
		// Note: for inline literals(0x20-0x3f), the value type is VT_LITERAL
		// and the nextWord member is initialized to the translated value
	};
	enum Register {
		A=0,B=1,C=2,X=3,Y=4,Z=5,I=6,J=7
	};
	
	ValueType val;
	Register reg; // Only for VT_REGISTER-VT_INDIRECT_REGISTER_OFFSET
	uint16_t nextWord;
};

struct DCPUInsn {
	DCPUOpcode op;
	DCPUValue a, b;
	uint16_t offset, nextOffset;
	uint8_t cycleCost;
};

// Hardware base class
struct DCPUHardwareDevice {
	virtual ~DCPUHardwareDevice() {};

	// Hardware device MUST return if it modifies any aspect of the CPU state. Note
	// that this function will be called from the simulation thread, so make sure
	// your handler is threadsafe.
	virtual uint8_t onInterrupt(uint16_t iNum, DCPUState* cpu)=0;
	
	// Hardware device should do nothing but return the cycle cost of the given
	// interrupt given a DCPUState - DO NOT MODIFY THE PASSED STATE.
	virtual uint8_t getCyclesForInterrupt(uint16_t iNum, DCPUState* cpu)=0;
};

// State of the DCPU for passing to assembler routines
struct DCPURegisterInfo {
	// Registers
	uint16_t a, b, c, x, y, z, i, j;
	uint16_t pc, sp, ex, ia;
	
	// Memory
	uint16_t *memory;
} __attribute__((packed));

// Full representation of the state of an emulated DCPU
struct DCPUState {
	DCPUState();
	~DCPUState();
	DCPUInsn decodeInsn();
	void loadFromFile(FILE* fptr);
	uint16_t getWord();
	uint16_t& operator[](uint16_t addr);
	
	DCPURegisterInfo info;

	// Interrupt queue
	uint8_t nInterrupts;
	uint8_t interruptQueue[256];
	
	// Hardware
	std::list<DCPUHardwareDevice*> hardware;
	
	// Threading and state tracking stuff
	uint64_t cycles; // Number of available cycles
	uint64_t elapsed; // Total elapsed cycles
};
