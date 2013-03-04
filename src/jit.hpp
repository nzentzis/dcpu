#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <list>
#include <sstream>
#include "dcpu.hpp"

#include "asmjit/AsmJit.h"

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
	uint32_t* m_chunkCosts;
	dcpu64Func* m_codeCache;
	// Keep a list of marked addrs to speed up freeing
	std::list<uint16_t> m_cacheAddrs;
};
