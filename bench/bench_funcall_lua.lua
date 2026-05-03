local function add(a, b)
    return a + b
end

local N = 50000000
local start = os.clock()
local sum = 0
for i = 1, N do
    sum = sum + add(i, 1)
end
local elapsed = os.clock() - start
print("sum = " .. sum)
print("time = " .. elapsed .. " seconds")
