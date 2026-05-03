function fib(n)
    if n < 2 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

local N = 10
local start = os.clock()
local result = 0
for i = 1, N do
    result = result + fib(30)
end
local elapsed = os.clock() - start
print("result = " .. result)
print("time = " .. elapsed .. " seconds")
