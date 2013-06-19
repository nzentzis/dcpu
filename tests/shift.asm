set a, 0xf0f0
shl a, 8
set [1000], ex

set b, 0x2121
shr b, 7
set [1001], ex

set c, 0x2
asr c, 0xf003
set [1002], ex

set x, 0xf003
asr x, 6

:loop
set pc, loop
