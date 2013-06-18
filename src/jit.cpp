#include "jit.hpp"
#include <vector>
#include <string.h>
#ifdef WIN32
#else
#include <sys/mman.h>
#endif

#define DISABLE_CYCLE_HOOK

using namespace AsmJit;

// Stores state that is global throughout the codegen. Deleted
// when codegen is finished.
struct CodeGenState {
	// Decrement every non-conditional insn. When 0, bind condEndLbl and
	// set to -1
	int8_t bindCtr;
	Label condEndLbl;
};

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

// Cycle hook - use this to check the interrupt status. If an interrupt is
// fired, this should return a nonzero value.
uint8_t cycleHook(DCPURegisterInfo* info) {
	if(!info->enableInterrupts) return 0;
	DCPUState* state = (DCPUState*)(info->statePtr);

	// Update hardware information
	// Check the interrupt queue
	if(!state->interruptQueue.empty()) {
		// Mark the ISR flag, meaning that when the generated code
		// returns after we do, the cycle function will catch this
		// and handle the next interrupt.
		state->isr = true;
		return 1;
	}
	return 0;
}

void emitCycleHook(AsmJit::Assembler& s, uint8_t cycles) {
#ifdef DISABLE_CYCLE_HOOK
	s.nop();
#else
	// Emit a call to the cycle hook function
	s.call((void*)&cycleHook);
	
	// Check for results
	Label okay = s.newLabel();
	s.cmp(rax, 0);
	s.je(okay);
	
	// Here, the cycle hook returned a nonzero value
	// so we should just return
	s.ret();
	
	// Ignore
	s.bind(okay);
#endif
}

void emitHeader(AsmJit::Assembler& s) {
	// No header - the caller does this for us now
}

void emitFooter(AsmJit::Assembler& s) {
	s.mov(eax, 0);
	s.ret();
}

// Fetch the value in the specified register into eax. Note that you really really should zero-extend
// this, as the value will run over from the next register if you don't. If you want sign-extension, that
// works too. Just note that if you don't zero-extend or sign-extend, the assembler will treat the register
// in the state structure as a 32-bit value, so you'll get two registers mashed together.
template<typename T>
void emitDCPUFetch(Assembler& s, DCPUValue r, T& reg, bool zx=true, bool sx=false) {
	switch(r.val) {
		case DCPUValue::VT_REGISTER:
			if(zx) {
				s.movzx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			}
			break;
		case DCPUValue::VT_INDIRECT_REGISTER:
			s.mov(rsi, qword_ptr(rdi, 32));
			s.movzx(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			if(zx) {
				s.movzx(reg, word_ptr(rsi, rdx));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rsi, rdx));
			}
			break;
		case DCPUValue::VT_INDIRECT_REGISTER_OFFSET:
			s.mov(rsi, qword_ptr(rdi, 32));
			s.movzx(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.add(rdx, r.nextWord);
			// We have to multiply by two so we get the byte address
			// instead of the word address
			s.shl(rdx, 1);
			if(zx) {
				s.movzx(reg, word_ptr(rsi, rdx));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rsi, rdx));
			}
			break;
		case DCPUValue::VT_PUSHPOP:
			s.mov(rdx, 0xffff);
			if(r.b) { // Push - [--SP]
				// Update and store the stack pointer
				s.movzx(rcx, word_ptr(rdi, 0x12));
				s.dec(rcx);
				s.mov(word_ptr(rdi, 0x12), cx);
				s.and_(rcx, rdx);
				
				// Convert to a byte address
				s.shl(rcx, 1);

				// Now read the memory - for science
				s.mov(rsi, qword_ptr(rdi, 0x20));
				if(zx) {
					s.movzx(reg, word_ptr(rsi, rcx));
				} else if(sx) {
					s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
				} else {
					s.mov(reg, word_ptr(rsi, rcx));
				}
			} else { // Pop - [SP++]
				// Figure out the word address
				s.movzx(rcx, word_ptr(rdi, 0x12));
				
				// Convert to byte address
				s.shl(rcx, 1);
				
				// Read memory
				s.mov(rsi, qword_ptr(rdi, 0x20));
				if(zx) {
					s.movzx(reg, word_ptr(rsi, rcx));
				} else if(sx) {
					s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
				} else {
					s.mov(reg, word_ptr(rsi, rcx));
				}
				
				// Update SP
				s.shr(rcx, 1);
				s.inc(rcx);
				s.mov(word_ptr(rdi, 0x12), cx);
			}
			break;
		case DCPUValue::VT_PEEK:
			s.mov(rdx, 0xffff);
			s.movzx(rcx, word_ptr(rdi, 0x12));
			s.shl(rcx, 1);
			s.mov(rsi, qword_ptr(rdi, 32));
			if(zx) {
				s.movzx(reg, word_ptr(rsi, rcx));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rsi, rcx));
			}
			break;
		case DCPUValue::VT_PICK:
			break;
		case DCPUValue::VT_SP:
			if(zx) {
				s.movzx(reg, word_ptr(rdi, 2*9));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rdi, 2*9));
			}
			break;
		case DCPUValue::VT_PC:
			if(zx) {
				s.movzx(reg, word_ptr(rdi, 2*8));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rdi, 2*8));
			}
			break;
		case DCPUValue::VT_EX:
			if(zx) {
				s.movzx(reg, word_ptr(rdi, 2*10));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rdi, 2*10));
			}
			break;
		case DCPUValue::VT_MEMORY:
			s.mov(rsi, qword_ptr(rdi, 32));
			if(zx) {
				s.movzx(reg, word_ptr(rsi, 2*r.nextWord));
			} else if(sx) {
				s.movsx(reg, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			} else {
				s.mov(reg, word_ptr(rsi, 2*r.nextWord));
			}
			break;
		case DCPUValue::VT_LITERAL:
			int16_t value = *((int16_t*)&r.nextWord);
			s.mov(reg, value);
			break;
	}
}

Mem getRegisterMemory(DCPUValue r) {
	return word_ptr(rdi, 2*((uint8_t)(r.reg)));
}

void emitDCPUSetPC(Assembler& s, uint16_t n) {
	// 16-bit values are 2 bytes each, offset 8 from the start of the struct
	s.mov(word_ptr(rdi, 2*8), n);
}

template<typename T>
void emitDCPUPut(Assembler& s, DCPUValue r, T &reg) {
	switch(r.val) {
		case DCPUValue::VT_REGISTER:
			s.mov(word_ptr(rdi, 2*((uint8_t)(r.reg))), reg);
			break;
		case DCPUValue::VT_INDIRECT_REGISTER:
			s.mov(rsi, qword_ptr(rdi, 32));
			s.movzx(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.mov(word_ptr(rsi, rdx), reg);
			break;
		case DCPUValue::VT_INDIRECT_REGISTER_OFFSET:
			s.mov(rsi, qword_ptr(rdi, 32));
			s.movzx(rdx, word_ptr(rdi, 2*((uint8_t)(r.reg))));
			s.add(rdx, r.nextWord);
			// We have to multiply by two so we get the byte address
			// instead of the word address
			s.shl(rdx, 1);
			s.mov(word_ptr(rsi, rdx), reg);
			break;
		case DCPUValue::VT_PUSHPOP:
			s.mov(rdx, 0xffff);
			if(r.b) { // Push - [--SP]
				// Update and store the stack pointer
				s.movzx(rcx, word_ptr(rdi, 0x12));
				s.dec(rcx);
				s.mov(word_ptr(rdi, 0x12), cx);
				s.and_(rcx, rdx);
				
				// Convert to a byte address
				s.shl(rcx, 1);

				// Now write
				s.mov(rsi, qword_ptr(rdi, 0x20));
				s.mov(word_ptr(rsi, rcx), reg);
			} else { // Pop - [SP++]
				// Figure out the word address
				s.movzx(rcx, word_ptr(rdi, 0x12));
				
				// Convert to byte address
				s.shl(rcx, 1);
				
				// Write memory
				s.mov(rsi, qword_ptr(rdi, 0x20));
				s.mov(word_ptr(rdi, rcx), reg);
				
				// Update SP
				s.shr(rcx, 1);
				s.inc(rcx);
				s.mov(word_ptr(rdi, 0x12), cx);
			}
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
			s.mov(rsi, qword_ptr(rdi, 32));
			s.mov(word_ptr(rsi, r.nextWord*2), reg);
			break;
		case DCPUValue::VT_LITERAL:
			// Fail silently
			break;
	}
}

template<typename T>
void emitCostCycles(Assembler& s, T &num) {
	s.sub(qword_ptr(rdi, 24), num);
}

// Proxy calls for the HWI, HWQ, and HWN instructions

uint16_t hardwareNumberQuery(DCPURegisterInfo* regInfo) {
	DCPUState* state = (DCPUState*)(regInfo->statePtr);
	return state->hardware.size();
}

void hardwareQuery(DCPURegisterInfo* regInfo, uint16_t n) {
	DCPUState* state = (DCPUState*)(regInfo->statePtr);
	DCPUHardwareInformation info = state->hardware[n]->getInformation();
	regInfo->a = info.hwID & 0x0000FFFF;
	regInfo->b = (info.hwID & 0xFFFF0000) >> 16;
	regInfo->c = info.hwRevision;
	regInfo->x = info.hwManufacturer & 0x0000FFFF;
	regInfo->y = (info.hwManufacturer & 0xFFFF0000) >> 16;
}

uint16_t hardwareInterrupt(DCPURegisterInfo* regInfo, uint16_t n) {
	DCPUState* state = (DCPUState*)(regInfo->statePtr);
	return state->hardware[n]->onInterrupt(state);
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
		if(m_codeCache[*i] != NULL)
			MemoryManager::getGlobal()->free(
					(void*)(m_codeCache[*i]));
	}
	
	// Free the cache arrays
	free(m_codeCache);
	free(m_chunkCosts);
}

void JITProcessor::inject(uint64_t cycles) {
	m_state.info.cycles += cycles;
	while(cycle());
}

bool JITProcessor::cycle() {
	// Check the current instruction pointer to see if it's in the code
	// cache
	if(m_codeCache[m_state.info.pc] == NULL) {
		// Generate new code for the instruction pointer
		generateCode();
	}
	
	// Check for queued cycles. The volatile pointer is a derpy trick to force
	// gcc to re-fetch the value of m_state.info.cycles from memory, after the
	// assembled code borks around with it. If it's not declared volatile or if
	// a pointer isn't used, it'll cache the result in a register and m_state.elapsed
	// will be 0 all the time.
	volatile DCPUState* st = &m_state;
	volatile uint32_t oldCycles = m_state.info.cycles;
	if(m_state.info.cycles < 0) return false;

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
	m_state.elapsed += (((int64_t)oldCycles)-st->info.cycles);
	if(m_state.isr) {
		if(m_state.interruptQueue.size() > 256) {
			// Halt and Catch Fire
			m_state.ignited = true;
			return false;
		}
		// Handle one interrupt
		uint16_t interrupt = m_state.interruptQueue.front();
		m_state.interruptQueue.pop();

		// Push PC and A to the stack
		m_state.info.memory[--m_state.info.sp] = m_state.info.pc;
		m_state.info.memory[--m_state.info.sp] = m_state.info.a;

		// Set up the environment for the interrupt handler
		m_state.info.a = interrupt;
		m_state.info.pc = m_state.info.ia;
	}
	return true;
}

void emitSET(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	if(inst.a.val == DCPUValue::VT_LITERAL) {
		// Shortcircuit
		emitDCPUPut(s, inst.b, inst.a.nextWord);
	} else {
		emitDCPUFetch(s, inst.a, ax, false);
		emitDCPUPut(s, inst.b, ax);
	}
}

void emitADD(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.b, eax);
	emitDCPUFetch(s, inst.a, ebx);
	s.add(eax, ebx);
	emitDCPUPut(s, inst.b, ax);
	dcpuEmitCarry(s, 1);
}

void emitSUB(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, ebx);
	emitDCPUFetch(s, inst.b, eax);
	s.sub(eax, ebx);
	emitDCPUPut(s, inst.b, ax);
	dcpuEmitCarry(s, 0xffff);
}

void emitMUL(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, ebx);
	emitDCPUFetch(s, inst.b, eax);
	
	// Multiply ax by bx, store lower part in ax and higher part in dx
	s.mul(bx);
	
	// Store the overflow first, since edx is a discretionary register
	// we don't want our result clobbered before we have the chance to
	// store it in the DCPU's register
	s.mov(word_ptr(rdi, 2*10), dx);
	
	// And store the lower part of the output
	emitDCPUPut(s, inst.b, ax);
}

void emitMLI(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	// Same as MUL, but using imul instead of mul
	emitDCPUFetch(s, inst.a, ebx);
	emitDCPUFetch(s, inst.b, eax);
	s.imul(bx);
	s.mov(word_ptr(rdi, 2*10), dx);
	emitDCPUPut(s, inst.b, ax);
}

void emitDIV(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, ebx);
	emitDCPUFetch(s, inst.b, eax);
	
	// Build a label for zero-checking
	Label doneLbl = s.newLabel();

	// Set up the division operation
	s.mov(ecx, eax); // Store B value
	s.xor_(edx, edx); // The second part can be zeroed since unsigned division
	s.cmp(ebx, edx);
	s.cmove(eax, edx);
	s.je(doneLbl);
	s.shl(eax, 16); // Shift 16 to compute the EX value
	s.div(ebx); // Get the new value for the EX register
	
	// Update EX register
	s.mov(word_ptr(rdi, 2*10), ax);
	
	// Divide for B register
	s.mov(eax, ecx);
	s.div(ebx); // Get the new value for the B register
	s.bind(doneLbl);
	emitDCPUPut(s, inst.b, ax);
}

void emitDVI(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitMOD(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitMDI(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitAND(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, eax);
	emitDCPUFetch(s, inst.b, ebx);
	s.and_(eax, ebx);
	emitDCPUPut(s, inst.b, ax);
}

void emitBOR(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, eax);
	emitDCPUFetch(s, inst.b, ebx);
	s.or_(eax, ebx);
	emitDCPUPut(s, inst.b, ax);
}

void emitXOR(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, eax);
	emitDCPUFetch(s, inst.b, ebx);
	s.xor_(eax, ebx);
	emitDCPUPut(s, inst.b, ax);
}

void emitSHR(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitASR(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitSHL(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
}

void emitADX(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, eax);
	emitDCPUFetch(s, inst.b, ebx);
	s.add(ebx, eax);
	s.mov(word_ptr(rdi, 2*10), 0);
	Label nocarry = s.newLabel();
	s.jnc(nocarry);
	s.mov(word_ptr(rdi, 2*10), 1);
	s.bind(nocarry);
	s.add(ebx, word_ptr(rdi, 2*10));
	
	Label done = s.newLabel();
	s.jnc(done);

	s.mov(word_ptr(rdi, 2*10), 1);
	s.bind(done);
	emitDCPUPut(s, inst.b, ebx);
}

void emitSBX(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitDCPUFetch(s, inst.a, eax);
	emitDCPUFetch(s, inst.b, ebx);
	s.add(ebx, eax);
	s.add(ebx, word_ptr(rdi, 2*10));
}

void emitSTI(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitSET(s, inst, cgs);
	s.add(word_ptr(rdi, 12), 1);
	s.add(word_ptr(rdi, 14), 1);
}

void emitSTD(Assembler& s, DCPUInsn inst, CodeGenState& cgs) {
	emitSET(s, inst, cgs);
	s.sub(word_ptr(rdi, 12), 1);
	s.sub(word_ptr(rdi, 14), 1);
}

void emitHWN(Assembler& s, DCPUInsn inst, CodeGenState cgs) {
	s.call((void*)&hardwareNumberQuery);
	emitDCPUPut(s, inst.a, eax);
}

void emitHWQ(Assembler& s, DCPUInsn inst, CodeGenState cgs) {
	emitDCPUFetch(s, inst.a, rsi);
	s.call((void*)&hardwareQuery);
	emitDCPUPut(s, inst.a, eax);
}

void emitHWI(Assembler& s, DCPUInsn inst, CodeGenState cgs) {
	// We have to return after executing an interrupt, because they
	// can modify arbitrary addresses or registers (including PC). Therefore
	// we need to go back to the JITProcessor wrapper so code invalidation
	// is handled properly.
	emitDCPUFetch(s, inst.a, rsi);
	s.call((void*)&hardwareInterrupt);
	emitCostCycles(s, eax);
	emitDCPUSetPC(s, inst.nextOffset);
	emitFooter(s);
}

bool isConditionalSigned(DCPUInsn i) {
	switch(i.op) {
		case DO_IFA:
		case DO_IFU:
			return true;
		default:
			return false;
	}
}

void emitConditional(Assembler& s, DCPUInsn inst, CodeGenState& cgs, uint32_t skipCost) {
	bool isSigned = isConditionalSigned(inst);
	emitDCPUFetch(s, inst.a, eax, !isSigned, isSigned);
	emitDCPUFetch(s, inst.b, ebx, !isSigned, isSigned);
	
	// Compare the two
	switch(inst.op) {
		case DO_IFB:
		case DO_IFC:
			s.test(ebx, eax);
			break;
		default:
			s.cmp(ebx,eax);
	}

	// localDontSkip, if jumped to, executes the condition's body
	Label localDontSkip = s.newLabel();
	switch(inst.op) {
		case DO_IFB:
			s.jnz(localDontSkip);
			break;
		case DO_IFC:
			s.jz(localDontSkip);
			break;
		case DO_IFE:
			s.je(localDontSkip);
			break;
		case DO_IFN:
			s.jne(localDontSkip);
			break;
		case DO_IFG: // Unsigned
			s.ja(localDontSkip);
			break;
		case DO_IFA: // Signed
			s.jg(localDontSkip);
			break;
		case DO_IFL: // Unsigned
			s.jb(localDontSkip);
			break;
		case DO_IFU: // Signed
			s.jl(localDontSkip);
			break;
		default:
			break;
	}
	emitCostCycles(s, skipCost);
	s.jmp(cgs.condEndLbl);
	s.bind(localDontSkip);
}

bool isConditionalInsn(DCPUInsn inst) {
	switch(inst.op) {
		case DO_IFB:
		case DO_IFC:
		case DO_IFE:
		case DO_IFN:
		case DO_IFG:
		case DO_IFA:
		case DO_IFL:
		case DO_IFU:
			return true;
		default:
			return false;
	}
}

// Called from the main generation loop whenever an IF* opcode is encountered. This
// function figures out the length of the conditional chain, figures out the cycle
// cost for each function to skip, sets up the code generation state's bindCtr member
// to let the caller know when to bind to the skip target, emits the assembly
// for all the conditionals in the chain, and finally sets up the program counter of
// the DCPUState to the instruction after the last IF in the chain.
void handleConditionalGeneration(Assembler& s, CodeGenState& cgs, DCPUState& st) {
	uint16_t savedPC = st.info.pc;

	// Skip forward and find the end of the conditional block
	// Keep track of the cycle cost of the first test failing
	uint32_t numSkipped = 0;
	while(isConditionalInsn(st.decodeInsn())) numSkipped++;

	// Set up code emission state
	cgs.condEndLbl = s.newLabel();

	// Reset PC and start emitting code
	st.info.pc = savedPC;
	DCPUInsn inst;
	while(isConditionalInsn(inst = st.decodeInsn())) {
		savedPC = st.info.pc;
		// Here, numSkipped determines the cycles that failing the test
		// and jumping costs. Since the first conditional will cost the
		// most, we just decrement the cost for each one, and the cost when
		// we reach the last conditional in the line will be 1.
		emitConditional(s, inst, cgs, numSkipped--);
	}
	
	// Restore PC to first non-conditional instruction and set up parameters
	st.info.pc = savedPC;
	cgs.bindCtr = 1;
}

void JITProcessor::generateCode() {
	// Save the CPU's program counter
	uint32_t oldPC = m_state.info.pc;

	// Create storage for the emitted instructions
	AsmJit::Assembler buf;
	CodeGenState state;
	state.bindCtr = -1;
	
	// Compile until we hit the next jump instruction
	DCPUInsn inst;
	uint32_t cost = 0;
	bool assembling = true;
	emitHeader(buf);
	while(assembling) {
		inst = m_state.decodeInsn();
		cost += inst.cycleCost;
		if(state.bindCtr == 0) {
			state.bindCtr = -1;
			buf.bind(state.condEndLbl);
		} else if(state.bindCtr > 0) {
			state.bindCtr--;
		}
		// Check for external opcodes
		switch(inst.op) {
			// Hardware interaction is done externally for now. Just set eax to 1 and
			// return.
			case DO_HWI:
			case DO_HWQ:
			case DO_HWN:
				buf.mov(eax, 1);
				buf.ret();
				assembling = false;
				continue;
			default:
				break;
		}
		
		// The only time we need to adjust PC is when we encounter an insn
		// that depends on it, since our code will always run as a block. This
		// will always hit on the last instruction since we stop assembling
		// when PC is changed.
		if(inst.a.val == DCPUValue::VT_PC || inst.b.val == DCPUValue::VT_PC) {
			emitDCPUSetPC(buf, inst.offset);
		}
		emitCostCycles(buf, inst.cycleCost);
		emitCycleHook(buf, inst.cycleCost);
		if(isConditionalInsn(inst)) {
			m_state.info.pc = inst.offset;
			handleConditionalGeneration(buf, state, m_state);
			continue;
		}
		switch(inst.op) {
			case DO_SET:
				emitSET(buf, inst, state);
				if(inst.b.val == DCPUValue::VT_PC) {
					if(state.bindCtr == -1) {
						// For jump instructions, just set PC and
						// return
						assembling = false;
					} else {
						// Make sure that the skipped instruction still emits a ret
						emitFooter(buf);
					}
				}
				break;
			case DO_ADD:
				emitADD(buf, inst, state);
				break;
			case DO_SUB:
				emitSUB(buf, inst, state);
				break;
			case DO_MUL:
				emitMUL(buf, inst, state);
				break;
			case DO_MLI:
				emitMLI(buf, inst, state);
				break;
			case DO_DIV:
				emitDIV(buf, inst, state);
				break;
			case DO_DVI:
				emitDVI(buf, inst, state);
				break;
			case DO_MOD:
				emitMOD(buf, inst, state);
				break;
			case DO_MDI:
				emitMDI(buf, inst, state);
				break;
			case DO_AND:
				emitAND(buf, inst, state);
				break;
			case DO_BOR:
				emitBOR(buf, inst, state);
				break;
			case DO_XOR:
				emitXOR(buf, inst, state);
				break;
			case DO_SHR:
				emitSHR(buf, inst, state);
				break;
			case DO_ASR:
				emitASR(buf, inst, state);
				break;
			case DO_SHL:
				emitSHL(buf, inst, state);
				break;
			case DO_STI:
				emitSTI(buf, inst, state);
				if(inst.b.val == DCPUValue::VT_PC) {
					if(state.bindCtr == -1) {
						assembling = false;
					} else {
						// Make sure that the skipped instruction still emits a ret
						emitFooter(buf);
					}
				}
				break;
			case DO_STD:
				emitSTD(buf, inst, state);
				if(inst.b.val == DCPUValue::VT_PC) {
					if(state.bindCtr == -1) {
						assembling = false;
					} else {
						// Make sure that the skipped instruction still emits a ret
						emitFooter(buf);
					}
				}
				break;
			case DO_ADX:
				emitADX(buf, inst, state);
				break;
			case DO_SBX:
				emitSBX(buf, inst, state);
				break;
			case DO_INT:
				break;
			case DO_IAG:
				break;
			case DO_JSR:
				break;
			case DO_IAS:
				break;
			case DO_RFI:
				break;
			case DO_IAQ:
				break;
			case DO_HWN:
				emitHWN(buf, inst, state);
				break;
			case DO_HWI:
				emitHWI(buf, inst, state);
				break;
			case DO_HWQ:
				emitHWQ(buf, inst, state);
				break;
			default:
				assembling = false;
				break;
		}
	}
	if(state.bindCtr >= 0) {
		buf.bind(state.condEndLbl);
	}
	emitFooter(buf);
	
	// Store the function in cache and restore the program counter
	m_codeCache[oldPC] = function_cast<dcpu64Func>(buf.make());
	m_chunkCosts[oldPC] = (cost == 0) ? 1 : cost;
	m_cacheAddrs.push_back(oldPC);
	m_state.info.pc = oldPC;
}

DCPUState& JITProcessor::getState() {
	return m_state;
}
