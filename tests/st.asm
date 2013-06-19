sti a, 7
sti b, 5
sti c, 3
std x, 2
set push, i
set push, j

:ml
add y, 1
ifl i, 100
sti pc, ml

set j, pop
set i, pop

:loop
set pc, loop
