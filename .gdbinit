set disassembly-flavor intel
layout split
focus src
layout regs
focus cmd
set args --cycles 1000 test.bin
break JITProcessor::cycle
r

