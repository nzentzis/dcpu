#include "jit.hpp"
#include <vector>
#include <string.h>
#ifdef WIN32
#else
#include <sys/mman.h>
#endif

using namespace AsmJit;

ExecutableBuffer::ExecutableBuffer(size_t len) {
	buffer = (uint8_t*)malloc(len);
	bufSize = len;
	setProtection(false);
}

ExecutableBuffer::~ExecutableBuffer() {
	setProtection(true);
	free(buffer);
}

void ExecutableBuffer::loadString(std::string s) {
	bufSize = s.length();
	if(buffer != NULL) free(buffer);
	buffer = (uint8_t*)malloc(bufSize);
	memcpy(buffer, s.c_str(), bufSize);
}

uint8_t& ExecutableBuffer::operator[](size_t idx) {
	return buffer[idx];
}

int ExecutableBuffer::setProtection(bool nx) {
	if(nx) {
#ifdef WIN32
	// Use the Windows-specific mprotect
#else
	// Use the normal mprotect
	return mprotect(buffer, bufSize, PROT_READ|PROT_WRITE);
#endif
	} else {
#ifdef WIN32
	// Use the Windows-specific mprotect
#else
	// Use the normal mprotect
	return mprotect(buffer, bufSize, PROT_READ|PROT_WRITE|PROT_EXEC);
#endif
	}
}

void* ExecutableBuffer::getBufferPtr() {
	return (void*)buffer;
}

JITCompilationContext::JITCompilationContext() {
}

JITCompilationContext::~JITCompilationContext() {
}

void JITCompilationContext::callCode(DCPUState* s, ExecutableBuffer &buf) {
}

/* Emission stuff. Mappings:
 * RDI (first parameter) is the DCPURegisterInfo pointer
 * RSI is swap space for any called routines (always clobbered)
 * EDX is swap space for any called routines (always clobbered)
 *
 */

// Read the passed register from the DCPURegisterInfo in rdi and
// put it into eax
void dcpuEmitRegisterRead(AsmJit::Assembler& s, DCPUValue::Register r) {
	// Read from DCPU register into eax
	s.movzx(eax, word_ptr(rdi, 2*((uint8_t)(r))));
}

// Write the passed register from eax into DCPURegisterInfo offset
void dcpuEmitRegisterWrite(AsmJit::Assembler& s, DCPUValue::Register r) {
	s.mov(word_ptr(rdi, 2*((uint8_t)(r))), eax);
}

void dcpuEmitSpecialRead(AsmJit::Assembler& s, DCPUValue::ValueType r) {
	uint8_t offs = 0;
	switch(r) {
		case DCPUValue::VT_SP:
			offs = 9;
			break;
		case DCPUValue::VT_PC:
			offs = 8;
			break;
		case DCPUValue::VT_EX:
			offs = 10;
			break;
		default:	// Never happens - just here to silence warning
			break;
	}
	s.movzx(eax, word_ptr(rdi, 2*offs));
}

void dcpuEmitSpecialWrite(AsmJit::Assembler& s, DCPUValue::ValueType r) {
	uint8_t offs = 0;
	switch(r) {
		case DCPUValue::VT_SP:
			offs = 9;
			break;
		case DCPUValue::VT_PC:
			offs = 8;
			break;
		case DCPUValue::VT_EX:
			offs = 10;
			break;
		default:
			break;
	}
	s.mov(word_ptr(rdi, 2*offs), eax);
}

// Take the word address in eax and set eax or ebx to the memory value
// at that address
void dcpuEmitMemoryRead(AsmJit::Assembler& s, bool toEBX=false) {
	// Fetch the base address for the CPU memory
	s.mov(rdx, word_ptr(rdi, 12));
	
	// Write the value in eax into the CPU memory
	s.movzx(eax, word_ptr(rdx, rax));
}

// Take the word address in eax and write the value in ebx to it
void dcpuEmitMemoryWrite(AsmJit::Assembler& s) {
	// Emit 'movq 24(%rdi), %rdx'
	s.mov(rdx, word_ptr(rdi, 12));
	
	// Emit 'movl %bx, (%rdx,%rbx,2)
	s.mov(word_ptr(rdx, rbx), bx);
}

// Takes the x86 carry flag and sets EX to its value
void dcpuEmitCarry(AsmJit::Assembler& s, uint16_t value) {
	s.push(eax);
	s.push(ebx);
	s.mov(eax, value);
	s.mov(ebx, value);
	s.cmovc(ebx, eax);
	s.mov(word_ptr(rdi, 20), ebx);
	s.pop(ebx);
	s.pop(eax);
}

void JITCompilationContext::emitCycleHook(AsmJit::Assembler& s, uint8_t cycles) {
}

void JITCompilationContext::emitHeader(AsmJit::Assembler& s) {
	// No header - the caller does this for us now
}

void JITCompilationContext::emitFooter(AsmJit::Assembler& s) {
	s.ret();
}

// Fetch the value in the specified register into eax
template<typename T>
void emitDCPUFetch(Assembler& s, DCPUValue r, T& reg) {
	switch(r.val) {
		case DCPUValue::VT_REGISTER:
			s.movzx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			break;
		case DCPUValue::VT_INDIRECT_REGISTER:
			s.mov(rsi, word_ptr(rdi, 12));
			s.mov(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.mov(reg, word_ptr(rsi, rdx));
			break;
		case DCPUValue::VT_INDIRECT_REGISTER_OFFSET:
			s.mov(rsi, word_ptr(rdi, 12));
			s.mov(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.add(rdx, r.nextWord);
			s.mov(reg, word_ptr(rsi, rdx));
			break;
		case DCPUValue::VT_PUSHPOP:	// In this case, it's a pop
			break;
		case DCPUValue::VT_PEEK:
			break;
		case DCPUValue::VT_PICK:
			break;
		case DCPUValue::VT_SP:
			s.mov(reg, word_ptr(rdi, 2*9));
			break;
		case DCPUValue::VT_PC:
			s.mov(reg, word_ptr(rdi, 2*8));
			break;
		case DCPUValue::VT_EX:
			s.mov(reg, word_ptr(rdi, 2*10));
			break;
		case DCPUValue::VT_MEMORY:
			s.mov(reg, word_ptr(rdi, 12+r.nextWord));
			break;
		case DCPUValue::VT_LITERAL:
			s.mov(reg, r.nextWord);
			break;
	}
}

Mem getRegisterMemory(DCPUValue r) {
	return word_ptr(rdi, 2*((uint8_t)(r.reg)));
}

template<typename T>
void emitDCPUPut(Assembler& s, DCPUValue r, T &reg) {
	switch(r.val) {
		case DCPUValue::VT_REGISTER:
			s.mov(word_ptr(rdi, 2*((uint8_t)(r.reg))), reg);
			break;
		case DCPUValue::VT_INDIRECT_REGISTER:
			s.mov(rsi, word_ptr(rdi, 12));
			s.mov(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.mov(word_ptr(rsi, rdx), reg);
			break;
		case DCPUValue::VT_INDIRECT_REGISTER_OFFSET:
			break;
		case DCPUValue::VT_PUSHPOP:	// In this case, it's a push
			break;
		case DCPUValue::VT_PEEK:
			break;
		case DCPUValue::VT_PICK:
			break;
		case DCPUValue::VT_SP:
			s.mov(word_ptr(rdi, 2*9), reg);
			break;
		case DCPUValue::VT_PC:
			s.mov(word_ptr(rdi, 2*8), reg);
			break;
		case DCPUValue::VT_EX:
			s.mov(word_ptr(rdi, 2*10), reg);
			break;
		case DCPUValue::VT_MEMORY:
			s.mov(word_ptr(rdi, 12+r.nextWord), reg);
			break;
		case DCPUValue::VT_LITERAL:
			// Fail silently
			break;
	}
}

JITProcessor::JITProcessor() {
	m_codeCache = (dcpu64Func*)malloc(sizeof(dcpu64Func)*0x10000);
	memset(m_codeCache, 0, sizeof(dcpu64Func)*0x10000);
	m_chunkCosts = (uint32_t*)malloc(sizeof(uint32_t)*0x10000);
	memset(m_chunkCosts, 0, sizeof(uint32_t)*0x10000);
}

JITProcessor::~JITProcessor() {
	// Free the code cache
	std::list<uint16_t>::iterator i;
	for(i=m_cacheAddrs.begin();i != m_cacheAddrs.end();i++) {
		if(m_codeCache[*i] != NULL) MemoryManager::getGlobal()->free((void*)(m_codeCache[*i]));
	}
	
	// Free the cache arrays
	free(m_codeCache);
	free(m_chunkCosts);
}

void JITProcessor::inject(uint64_t cycles) {
	m_state.cycles += cycles;
	while(cycle());
}

bool JITProcessor::cycle() {
	// Check the current instruction pointer to see if it's in the code cache
	if(m_codeCache[m_state.info.pc] == NULL) {
		// Generate new code for the instruction pointer
		generateCode();
	}
	
	// Check for queued cycles
	uint32_t cost = m_chunkCosts[m_state.info.pc];
	if(m_state.cycles < cost) return false;
	m_state.cycles -= cost;
	m_state.elapsed += cost;

	// Execute the code at the instruction pointer
	dcpu64Func fptr = m_codeCache[m_state.info.pc];
	
	// Set up the environment for the compiled code and jump to it
	// This block:
	//	Pushes all registers
	//	Sets up the state info in rdi
	//	Calls the compiled code
	//	Restores registers
	asm volatile(
			"push %%rax\n\t"
			"push %%rbx\n\t"
			"push %%rcx\n\t"
			"push %%rdx\n\t"
			"push %%rsi\n\t"
			"push %%rdi\n\t"
			"mov %1, %%rdi\n\t"
			"call *%0\n\r"
			"pop %%rdi\n\t"
			"pop %%rsi\n\t"
			"pop %%rdx\n\t"
			"pop %%rcx\n\t"
			"pop %%rbx\n\t"
			"pop %%rax\n\t"
			:
			: "r"(fptr), "r"(&(m_state.info))
			);
	return true;
}

void emitSET(Assembler& s, DCPUInsn inst, JITCompilationContext& ctx) {
	ctx.emitCycleHook(s, inst.cycleCost);
	if(inst.a.val == DCPUValue::VT_LITERAL) {
		// Shortcircuit
		emitDCPUPut(s, inst.b, inst.a.nextWord);
	} else {
		emitDCPUFetch(s, inst.a, eax);
		emitDCPUPut(s, inst.b, eax);
	}
}

void emitADD(Assembler& s, DCPUInsn inst, JITCompilationContext& ctx) {
	ctx.emitCycleHook(s, inst.cycleCost);
	emitDCPUFetch(s, inst.b, eax);
	emitDCPUFetch(s, inst.a, ebx);
	s.add(eax, ebx);
	emitDCPUPut(s, inst.b, eax);
	dcpuEmitCarry(s, 1);
}

void emitSUB(Assembler& s, DCPUInsn inst, JITCompilationContext& ctx) {
	ctx.emitCycleHook(s, inst.cycleCost);
	emitDCPUFetch(s, inst.a, ebx);
	emitDCPUFetch(s, inst.b, eax);
	s.sub(eax, ebx);
	emitDCPUPut(s, inst.b, eax);
	dcpuEmitCarry(s, 0xffff);
}

void JITProcessor::generateCode() {
	// Save the CPU's program counter
	uint32_t oldPC = m_state.info.pc;

	// Create storage for the emitted instructions
	AsmJit::Assembler buf;
	
	// Compile until we hit the next jump instruction
	DCPUInsn inst;
	uint32_t cost = 0;
	bool assembling = true;
	m_ctx.emitHeader(buf);
	while(assembling) {
		inst = m_state.decodeInsn();
		cost += inst.cycleCost;
		switch(inst.op) {
			case DO_SET:
				emitSET(buf, inst, m_ctx);
				if(inst.b.val == DCPUValue::VT_PC) {
					// For jump instructions, just set PC and
					// return
					assembling = false;
				}
				break;
			case DO_ADD:
				emitADD(buf, inst, m_ctx);
				break;
			case DO_SUB:
				emitSUB(buf, inst, m_ctx);
				break;
			// Hardware interaction is done externally for now
			case DO_HWI:
			case DO_HWQ:
			case DO_HWN:
				assembling = false;
				break;
			default:
				break;
		}
	}
	m_ctx.emitFooter(buf);
	
	// Store the function in cache and restore the program counter
	m_codeCache[oldPC] = function_cast<dcpu64Func>(buf.make());
	m_chunkCosts[oldPC] = cost;
	m_cacheAddrs.push_back(oldPC);
	m_state.info.pc = oldPC;
}

DCPUState& JITProcessor::getState() {
	return m_state;
}
