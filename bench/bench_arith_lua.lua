local N = 100000000
local a = 0
local b = 1
local c = 2
local start = os.clock()
for i = 1, N do
    a = a + b * c
end
local elapsed = os.clock() - start
print("arith: a=" .. a .. " time=" .. elapsed)
