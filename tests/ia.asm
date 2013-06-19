ias interruptHandler
iag a
ife a, interruptHandler
set c, 3
int 1
set pc, loop

:interruptHandler
set peek, 1
set b, 2
rfi 1

:loop
set pc, loop
