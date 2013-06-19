set disassembly-flavor intel
layout split
focus src
layout regs
focus cmd
set args test.bin
break JITProcessor::cycle
r

