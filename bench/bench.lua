-- Lua 5.5 性能基准测试

-- 测试 1: 算术运算 (1000万次)
local N1 = 10000000
local start1 = os.clock()
local sum1 = 0
for i = 1, N1 do
    sum1 = sum1 + i
    if sum1 > 100000000 then
        sum1 = sum1 - 100000000
    end
end
local t1 = (os.clock() - start1) * 1000
print(string.format("arithmetic: %.3f ms", t1))

-- 测试 2: 循环 (1亿次)
local N2 = 100000000
local start2 = os.clock()
local sum2 = 0
for i = 1, N2 do
    sum2 = sum2 + i
end
local t2 = (os.clock() - start2) * 1000
print(string.format("loop: %.3f ms", t2))

-- 测试 3: 函数调用 (5000万次)
local function add(a, b)
    return a + b
end

local N3 = 50000000
local start3 = os.clock()
local sum3 = 0
for i = 1, N3 do
    sum3 = sum3 + add(i, 1)
end
local t3 = (os.clock() - start3) * 1000
print(string.format("function_call: %.3f ms", t3))

-- 测试 4: 简单循环 (200万次)
local N4 = 2000000
local start4 = os.clock()
local sum4 = 0
for i = 1, N4 do
    sum4 = sum4 + 1
end
local t4 = (os.clock() - start4) * 1000
print(string.format("simple_loop: %.3f ms", t4))

local total = t1 + t2 + t3 + t4
print(string.format("total: %.3f ms", total))
