#include "clock.hpp"
#include <boost/bind.hpp>

Clock::Clock(DCPUState* cpu) : cpu(cpu) {
}

uint8_t Clock::onInterrupt(DCPUState* cpu) {
	uint16_t a = cpu->info.a;
	uint16_t b = cpu->info.b;
	switch(a) {
		case 0:
			timeDivisor = b;
			lastUnitSetTime = clock.now();
			lastTick = lastUnitSetTime;
			if(executor != NULL) {
				executor->interrupt();
				delete executor;
			}
			executor = new boost::thread(boost::bind(&Clock::runThread, this, _1), atomic_time(b));
			break;
		case 1: {
			// Here, B is the number of units of size atomic_time
			atomic_time dur = boost::chrono::duration_cast<atomic_time>(clock.now() - lastUnitSetTime);
			cpu->info.c = dur.count() / timeDivisor;
			}
			break;
		case 2:
			message = b;
			break;
	}
	return 0;
}

uint8_t Clock::getCyclesForInterrupt(uint16_t i, DCPUState* cpu) {
	return 0;
}

DCPUHardwareInformation Clock::getInformation() {
	DCPUHardwareInformation inf;
	inf.hwID = 0x12d0b402;
	inf.hwRevision = 1;
	inf.hwManufacturer = 0;
	return inf;
}

void Clock::runThread(atomic_time diff) {
	while(message != 0) {
		cpu->m_interruptMutex.lock();
		cpu->interruptQueue.push(message);
		cpu->m_interruptMutex.unlock();
		boost::this_thread::sleep_for(diff);
	}
}
