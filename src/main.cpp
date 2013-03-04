#include <stdio.h>
#include <stdlib.h>
#include "dcpu.hpp"
#include "jit.hpp"
#include <boost/chrono.hpp>

void printUsage(char* name) {
	printf("Usage:\n\t%s [binary file]\n", name);
}

void printInsn(DCPUInsn i) {
	printf("Insn: %d %d %d %d %d %d\n", i.op, i.cycleCost, i.a.val, i.a.nextWord, i.b.val, i.b.nextWord);
}

int main(int argc, char **argv) {
	if(argc < 2) {
		printUsage(argv[0]);
		return 0;
	}
	
	JITProcessor proc;
	boost::chrono::high_resolution_clock clk;
	proc.getState().loadFromFile(fopen(argv[1], "rb"));
	printf("Performing measurement...");
	boost::chrono::high_resolution_clock::time_point start = clk.now();
	proc.inject(1000000);
	boost::chrono::high_resolution_clock::time_point end = clk.now();
	printf("Complete\n");
	boost::chrono::high_resolution_clock::duration d = end-start;
	boost::chrono::nanoseconds ns = d;
	
	DCPURegisterInfo i = proc.getState().info;
	printf("Final Register Values:\n\tA:%d\tB:%d\tC:%d\tX:%d\tY:%d\tZ:%d\tI:%d\tJ:%d\n", i.a, i.b, i.c, i.x, i.y, i.z, i.i, i.j);
	printf("Time Elapsed: %lu ns\n", ns.count());
	boost::chrono::duration<double, boost::ratio<1> > dsecs = ns;
	printf("Clock Frequency: %f Hz\n", proc.getState().elapsed/dsecs.count());
	printf("Elapsed Clocks: %d\n", proc.getState().elapsed);
}
