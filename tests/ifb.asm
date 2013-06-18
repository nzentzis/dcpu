set x, 0x2100

ifb x, 0xff00
set a, 1

ifb x, 0xff
set pc, fail
set b, 2

fail:
set pc, fail
