#include "dcpu.hpp"
#include <string.h>
#include <boost/phoenix/stl/algorithm/iteration.hpp>
#include <boost/phoenix/object/delete.hpp>
#include <boost/phoenix/core/argument.hpp>

DCPUState::DCPUState() {
	memset(&info, 0, sizeof(DCPURegisterInfo));
	info.memory = (uint16_t*)malloc(0x10000*sizeof(uint16_t));
	elapsed = info.cycles = 0;
	memset(info.memory, 0, 0x10000*sizeof(uint16_t));
	info.statePtr = (void*)this;
	ignited = isr = false;
}

DCPUState::~DCPUState() {
	boost::phoenix::for_each(hardware, (boost::phoenix::delete_(boost::phoenix::placeholders::_1)));
	free(info.memory);
}

DCPUInsn DCPUState::decodeInsn() {
	DCPUInsn insn;
	insn.cycleCost = 0;
	insn.offset = info.pc;
	uint16_t opc = getWord();
	uint8_t baseOpcode = (opc & 0x1f);
	uint8_t fieldA = (opc & 0xfc00) >> 10;
	uint8_t fieldB = (opc & 0x03e0) >> 5;
	insn.a.b = false;
	insn.b.b = true;
	
	// Process field A
	switch(fieldA) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			insn.a.val = DCPUValue::VT_REGISTER;
			insn.a.reg = (DCPUValue::Register)fieldA;
			break;
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:
			insn.a.val = DCPUValue::VT_INDIRECT_REGISTER;
			insn.a.reg = (DCPUValue::Register)(fieldA-0x08);
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			insn.a.val = DCPUValue::VT_INDIRECT_REGISTER_OFFSET;
			insn.a.reg = (DCPUValue::Register)(fieldA-0x10);
			insn.a.nextWord = getWord();
			insn.cycleCost++; // 1-cycle cost for memory access
			break;
		case 0x18:
			insn.a.val = DCPUValue::VT_PUSHPOP;
			break;
		case 0x19:
			insn.a.val = DCPUValue::VT_PEEK;
			break;
		case 0x1a:
			insn.a.val = DCPUValue::VT_PICK;
			insn.a.nextWord = getWord();
			break;
		case 0x1b:
			insn.a.val = DCPUValue::VT_SP;
			break;
		case 0x1c:
			insn.a.val = DCPUValue::VT_PC;
			break;
		case 0x1d:
			insn.a.val = DCPUValue::VT_EX;
			break;
		case 0x1e:
			insn.a.val = DCPUValue::VT_MEMORY;
			insn.cycleCost++;
			insn.a.nextWord = getWord();
			break;
		case 0x1f:
			insn.a.val = DCPUValue::VT_LITERAL;
			insn.cycleCost++;
			insn.a.nextWord = getWord();
			break;
		default:
			insn.a.val = DCPUValue::VT_LITERAL;
			insn.a.nextWord = ((uint16_t)fieldA - 0x21);
			break;
	}

	if(baseOpcode == 0) {
		switch(fieldB) {
			case 0x01: // JSR
				insn.cycleCost += 3;
				insn.op = DO_JSR;
				break;
			case 0x08: // INT
				insn.cycleCost += 4;
				insn.op = DO_INT;
				break;
			case 0x09: // IAG
				insn.cycleCost += 1;
				insn.op = DO_IAG;
				break;
			case 0x0a: // IAS
				insn.cycleCost += 1;
				insn.op = DO_IAS;
				break;
			case 0x0b: // RFI
				insn.cycleCost += 3;
				insn.op = DO_RFI;
				break;
			case 0x0c: // IAQ
				insn.cycleCost += 2;
				insn.op = DO_IAQ;
				break;
			case 0x10: // HWN
				insn.cycleCost += 2;
				insn.op = DO_HWN;
				break;
			case 0x11: // HWQ
				insn.cycleCost += 4;
				insn.op = DO_HWQ;
				break;
			case 0x12: // HWI
				insn.cycleCost += 4;
				// NOTE: The additional hardware cost will not be added
				// here, but will be handled in the JITed code
				insn.op = DO_HWI;
				break;
			default:
				insn.op = DO_INVALID;
				break;
		}
	} else {
		switch(fieldB) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
				insn.b.val = DCPUValue::VT_REGISTER;
				insn.b.reg = (DCPUValue::Register)fieldB;
				break;
			case 0x08:
			case 0x09:
			case 0x0a:
			case 0x0b:
			case 0x0c:
			case 0x0d:
			case 0x0e:
			case 0x0f:
				insn.b.val = DCPUValue::VT_INDIRECT_REGISTER;
				insn.b.reg = (DCPUValue::Register)(fieldB-0x08);
				break;
			case 0x10:
			case 0x11:
			case 0x12:
			case 0x13:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
				insn.b.val = DCPUValue::VT_INDIRECT_REGISTER_OFFSET;
				insn.b.reg = (DCPUValue::Register)(fieldB-0x10);
				insn.b.nextWord = getWord();
				insn.cycleCost++; // 1-cycle cost for memory access
				break;
			case 0x18:
				insn.b.val = DCPUValue::VT_PUSHPOP;
				break;
			case 0x19:
				insn.b.val = DCPUValue::VT_PEEK;
				break;
			case 0x1a:
				insn.b.val = DCPUValue::VT_PICK;
				insn.b.nextWord = getWord();
				break;
			case 0x1b:
				insn.b.val = DCPUValue::VT_SP;
				break;
			case 0x1c:
				insn.b.val = DCPUValue::VT_PC;
				break;
			case 0x1d:
				insn.b.val = DCPUValue::VT_EX;
				break;
			case 0x1e:
				insn.b.val = DCPUValue::VT_MEMORY;
				insn.b.nextWord = getWord();
				insn.cycleCost++;
				break;
			case 0x1f:
				insn.b.val = DCPUValue::VT_LITERAL;
				insn.b.nextWord = getWord();
				insn.cycleCost++;
				break;
			default:
				insn.b.val = DCPUValue::VT_LITERAL;
				insn.b.nextWord = ((uint16_t)fieldB - 0x21);
				break;
		}
		
		// Decode the instruction
		switch(baseOpcode) {
			printf("%d\n", baseOpcode);
			case 0x01:
				insn.op = DO_SET;
				insn.cycleCost += 1;
				break;
			case 0x02:
				insn.op = DO_ADD;
				insn.cycleCost += 2;
				break;
			case 0x03:
				insn.op = DO_SUB;
				insn.cycleCost += 2;
				break;
			case 0x04:
				insn.op = DO_MUL;
				insn.cycleCost += 2;
				break;
			case 0x05:
				insn.op = DO_MLI;
				insn.cycleCost += 2;
				break;
			case 0x06:
				insn.op = DO_DIV;
				insn.cycleCost += 3;
				break;
			case 0x07:
				insn.op = DO_DVI;
				insn.cycleCost += 3;
				break;
			case 0x08:
				insn.op = DO_MOD;
				insn.cycleCost += 3;
				break;
			case 0x09:
				insn.op = DO_MDI;
				insn.cycleCost += 3;
				break;
			case 0x0a:
				insn.op = DO_AND;
				insn.cycleCost += 1;
				break;
			case 0x0b:
				insn.op = DO_BOR;
				insn.cycleCost += 1;
				break;
			case 0x0c:
				insn.op = DO_XOR;
				insn.cycleCost += 1;
				break;
			case 0x0d:
				insn.op = DO_XOR;
				insn.cycleCost += 1;
				break;
			case 0x0e:
				insn.op = DO_ASR;
				insn.cycleCost += 1;
				break;
			case 0x0f:
				insn.op = DO_SHL;
				insn.cycleCost += 1;
				break;
			case 0x10:
				insn.op = DO_IFB;
				insn.cycleCost += 2;
				break;
			case 0x11:
				insn.op = DO_IFC;
				insn.cycleCost += 2;
				break;
			case 0x12:
				insn.op = DO_IFE;
				insn.cycleCost += 2;
				break;
			case 0x13:
				insn.op = DO_IFN;
				insn.cycleCost += 2;
				break;
			case 0x14:
				insn.op = DO_IFG;
				insn.cycleCost += 2;
				break;
			case 0x15:
				insn.op = DO_IFA;
				insn.cycleCost += 2;
				break;
			case 0x16:
				insn.op = DO_IFL;
				insn.cycleCost += 2;
				break;
			case 0x17:
				insn.op = DO_IFU;
				insn.cycleCost += 2;
				break;
			case 0x1a:
				insn.op = DO_ADX;
				insn.cycleCost += 3;
				break;
			case 0x1b:
				insn.op = DO_SBX;
				insn.cycleCost += 3;
				break;
			case 0x1e:
				insn.op = DO_STI;
				insn.cycleCost += 2;
				break;
			case 0x1f:
				insn.op = DO_STD;
				insn.cycleCost += 2;
				break;
			default:
				insn.op = DO_INVALID;
		}
	}

	insn.nextOffset = info.pc;
	return insn;
}

uint16_t DCPUState::getWord() {
	return info.memory[info.pc++];
}

uint16_t& DCPUState::operator[](uint16_t idx) {
	return info.memory[idx];
}

// Load a DCPU memory image from the passed file handle. If translate
// is true, swap byte ordering on each 16-bit word as the file is
// read in.
void DCPUState::loadFromFile(FILE* fptr, bool translate) {
	uint16_t word;
	uint8_t  t1, t2;
	uint16_t *bufptr = info.memory;
	while(fread(&word, 2, 1, fptr) == 1) {
		if(translate) {
			t1 = word & 0xFF;
			t2 = (word >> 8) & 0xFF;
			*(bufptr++) = (t1 << 8) | t2;
		} else {
			*(bufptr++) = word;
		}
	}
}

// Write the memory image of the DCPU into the passed file handle. If
// translate is set, write in big-endian format.
void DCPUState::writeToFile(FILE* fptr, bool translate) {
	uint16_t word;
	uint8_t t1, t2;
	unsigned int i;
	for(i=0;i<0x10000;i++) {
		word = info.memory[i];
		if(translate) {
			t1 = word & 0xFF;
			t2 = (word >> 8) & 0xff;
			word = (t1 << 8) | t2;
		}
		fwrite(&word, 2, 1, fptr);
	}
}
