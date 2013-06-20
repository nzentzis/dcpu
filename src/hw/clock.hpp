#pragma once
#include <stdint.h>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include "../dcpu.hpp"

class Clock : public DCPUHardwareDevice {
	typedef boost::chrono::duration<uint64_t, boost::ratio<60,0xffff> > atomic_time;
public:
	Clock(DCPUState* cpu);
	uint8_t onInterrupt(DCPUState* cpu);
	uint8_t getCyclesForInterrupt(uint16_t iNum, DCPUState* cpu);
	DCPUHardwareInformation getInformation();

private:
	void runThread(atomic_time diff);

	uint16_t timeDivisor;
	uint16_t message;
	boost::chrono::steady_clock clock;
	boost::chrono::time_point<boost::chrono::steady_clock> lastUnitSetTime;
	boost::chrono::time_point<boost::chrono::steady_clock> lastTick;
	boost::thread *executor;
	DCPUState* cpu;
};
