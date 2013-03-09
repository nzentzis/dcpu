#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include "dcpu.hpp"
#include "jit.hpp"
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/program_options.hpp>

#define BENCHMARK_CYCLES 100000000

// The smallest number of cycles that will be executed at a time
#define CYCLE_ATOM 100

using namespace std;
namespace po = boost::program_options;
namespace chron = boost::chrono;

std::string makeFancyUnit(double val, char* units) {
	int i;
	const char* prefixes[] = { "y","z","a","f","p","n","u","m","","k","M","G","T","P","E","Z","Y" };
	for(i=0;val > 1000;i++,val /= 1000.0f);
	for(;val < 1;i--,val *= 1000.0f);
	char buf[1024];
	std::string unit = std::string(prefixes[i+8])+std::string(units);
	snprintf(buf, 1023, "%f %s", val, unit.c_str());
	return std::string(buf);
}

void printInsn(DCPUInsn i) {
	printf("Insn: %d %d %d %d %d %d\n", i.op, i.cycleCost, i.a.val, i.a.nextWord, i.b.val, i.b.nextWord);
}

int main(int argc, char **argv) {
	po::options_description optDesc;
	optDesc.add_options()
		("sped", "Attach a SPED-3 Suspended Particle Exciter Display to the simulated DCPU")
		("lem", "Attach a LEM1802 Low Energy Monitor to the simulated DCPU")
		("bench", "Enable benchmarking mode. No hardware is attached, and statistics on emulation speed will be printed when emulation is complete")
		("profile", "Enable profiling mode. In profiling mode, tracepoints are generated in the generated machine code and a file with per-instruction statistics will be emitted")
		("test", "Enable testing mode. After emulation, the machine state will be dumped to the console")
		("test-mem", "Enable memory dumps after emulation in testing mode")
		("dump-file", po::value<std::string>()->default_value("dcpu.mem"), "The file to dump memory to")
		("cycles", po::value<uint64_t>()->default_value(0), "Limit the number of cycles the emulator can run for")
		("speed", po::value<float>(), "Maximum speed in KHz the emulated DCPU will run at")
		("help", "Print a help message")
		("image", po::value<std::string>(), "The program image to load")
		("little-endian,l", "Load a little-endian input file instead of a big-endian one")
	;
	
	po::positional_options_description posOptDesc;
	posOptDesc.add("image", 1);
	
	po::variables_map vmap;
	po::store(po::command_line_parser(argc, argv).options(optDesc).positional(posOptDesc).run(), vmap);
	po::notify(vmap);
	
	if(vmap.count("image") == 0 || vmap.count("help") > 0) {
		optDesc.print(std::cout);
		fprintf(stderr, "ERROR: Program image is required\n");
		return 1;
	}
	
	JITProcessor proc;
	
	// Load the program
	FILE* loadFile = fopen(vmap["image"].as<std::string>().c_str(), "rb");
	if(loadFile != NULL) {
		proc.getState().loadFromFile(loadFile, vmap.count("little-endian")==0);
		fclose(loadFile);
	} else {
		fprintf(stderr, "ERROR: Cannot open input file\n");
		return 1;
	}
	
	// Attach hardware to the processor
	if(vmap.count("bench") == 0) { // benchmarking mode disables all hardware and forces a limited number of cycles
		// Attach clock
		
		if(vmap.count("sped")) {
			// Attach a SPED-3
		}
		if(vmap.count("lem")) {
			// Attach a LEM1802
		}

		// Check whether we have any windows to host a keyboard in and attach one if we can
	}

	// Start hardware threads if required
	
	// Special behavior for benchmarking mode
	bool benchmarking = (vmap.count("bench") != 0);
	boost::chrono::high_resolution_clock clk;
	boost::chrono::high_resolution_clock::time_point start;
	if(benchmarking) {
		printf("Performing measurement...");
		fflush(stdout);
		start = clk.now();
	}
	
	// Run the processor
	if(vmap.count("speed")) {
		// The speed is limited, so we have to do more complex cycle limiting
		float maxSpeed = vmap["speed"].as<float>()*1000;
		boost::chrono::high_resolution_clock::time_point nextCycle;
		boost::chrono::duration<double, boost::ratio<1> > cycleTime(1.0f/maxSpeed);
		chron::high_resolution_clock::duration clkCycles =
			chron::round<chron::high_resolution_clock::duration>(cycleTime);
		if(vmap.count("cycles")) {
			int64_t cycles = vmap["cycles"].as<uint64_t>();
			for(;cycles > 0;cycles -= CYCLE_ATOM) {
				nextCycle = clk.now()+(clkCycles*CYCLE_ATOM);
				proc.inject(CYCLE_ATOM);
				boost::this_thread::sleep_until(nextCycle);
			}
		} else {
			while(true) {
				nextCycle = clk.now()+(clkCycles*CYCLE_ATOM);
				proc.inject(CYCLE_ATOM);
				boost::this_thread::sleep_until(nextCycle);
			}
		}
	} else {
		if(vmap.count("cycles")) {
			proc.inject(vmap["cycles"].as<uint64_t>());
		} else {
			while(true) proc.inject(10000000);
		}
	}

	if(benchmarking) {
		boost::chrono::high_resolution_clock::time_point end = clk.now();
		printf("Complete\n");
		boost::chrono::high_resolution_clock::duration d = end-start;
		boost::chrono::nanoseconds ns = d;
		boost::chrono::duration<double, boost::ratio<1> > dsecs = ns;

		printf("Time Elapsed: %s\n", makeFancyUnit(dsecs.count(), "s").c_str());
		double freq = proc.getState().elapsed/dsecs.count();
		printf("Clock Frequency: %s\n", makeFancyUnit(freq, "Hz").c_str());
		printf("Elapsed Clocks: %d\n", proc.getState().elapsed);
	}
	if(vmap.count("test")) {
		DCPURegisterInfo i = proc.getState().info;
		printf("A  = %04x\n", i.a);
		printf("B  = %04x\n", i.b);
		printf("C  = %04x\n", i.c);
		printf("X  = %04x\n", i.x);
		printf("Y  = %04x\n", i.y);
		printf("Z  = %04x\n", i.z);
		printf("I  = %04x\n", i.i);
		printf("J  = %04x\n", i.j);
		printf("PC = %04x\n", i.pc);
		printf("EX = %04x\n", i.ex);
		printf("IA = %04x\n", i.ia);
		printf("SP = %04x\n", i.sp);
	}
	if(vmap.count("test-mem")) {
		FILE* dump = fopen(vmap["dump-file"].as<std::string>().c_str(), "wb");
		if(dump == NULL) {
			fprintf(stderr, "ERROR: Cannot open memory dump file for writing\n");
			return 1;
		}
		proc.getState().writeToFile(dump, true);
		fclose(dump);
	}
}
