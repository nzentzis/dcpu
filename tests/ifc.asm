set x, 0xff00

ifc x, 0x00ff
set a, 1
ifc x, 0xf0f0
set pc, end
set b, 2
end:
set pc, end
