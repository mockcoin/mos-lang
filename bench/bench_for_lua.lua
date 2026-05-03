local N = 100000000
local sum = 0
local start = os.clock()
for i = 1, N do
    sum = sum + i
end
local elapsed = os.clock() - start
print("Lua for loop: sum=" .. sum .. " time=" .. elapsed)
