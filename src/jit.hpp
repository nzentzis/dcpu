#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <list>
#include <sstream>
#include "dcpu.hpp"

#include "asmjit/AsmJit.h"

// Instruction buffer used in JITing
class ExecutableBuffer {
public:
	ExecutableBuffer(size_t len);
	~ExecutableBuffer();

	void loadString(std::string s);
	uint8_t& operator[](size_t idx);
	int setProtection(bool nx);
	void* getBufferPtr();

private:
	uint8_t* buffer;
	size_t bufSize;
};

// Representation of the global JIT context used on a per-CPU basis, and handles
// the storage of functions common to every execution chunk in the program. Also
// provides helper functions to resolve a symbol table and to emit x86 instructions
// and holds the code cache for a CPU's JIT emulation context.
class JITCompilationContext {
public:
	JITCompilationContext();
	~JITCompilationContext();
	
	void callCode(DCPUState* s, ExecutableBuffer &buf);
	
	// Emission functions
	void emitCycleHook(AsmJit::Assembler& s, uint8_t cycles);
	void emitFooter(AsmJit::Assembler& s);
	void emitHeader(AsmJit::Assembler& s);
	
	void emitPCIncrement(AsmJit::Assembler& s, uint8_t n);

private:
};

typedef void (*dcpu64Func)(DCPURegisterInfo* ri);

class JITProcessor {
public:
	JITProcessor();
	~JITProcessor();

	void inject(uint64_t cycles);
	DCPUState& getState();
private:
	bool cycle();
	void generateCode(); // Generate and cache the code for the current PC

	DCPUState m_state;
	JITCompilationContext m_ctx;
	uint32_t* m_chunkCosts;
	dcpu64Func* m_codeCache;
	std::list<uint16_t> m_cacheAddrs; // Keep a list of marked addrs to speed up freeing
};
