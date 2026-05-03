local N = 10000000
local start = os.clock()
local sum = 0
for i = 1, N do
    sum = sum + i
    if sum > 100000000 then
        sum = sum - 100000000
    end
end
local elapsed = os.clock() - start
print("sum = " .. sum)
print("time = " .. elapsed .. " seconds")
