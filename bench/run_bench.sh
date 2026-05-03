#!/bin/bash
# 性能基准测试运行脚本

echo "========================================="
echo "Mos vs Lua 5.5 vs Python3 性能对比测试"
echo "========================================="
echo ""

cd /Users/yun/git/lua/mos-src

echo "--- 运行 Mos 测试 (3次取平均) ---"
mos_total=0
for i in 1 2 3; do
    result=$(./mos bench/bench.mos 2>&1 | grep "total:" | awk '{print $2}')
    echo "  第$i次: $result ms"
    mos_total=$(echo "$mos_total + $result" | bc)
done
mos_avg=$(echo "scale=3; $mos_total / 3" | bc)
echo "  Mos 平均: $mos_avg ms"
echo ""

echo "--- 运行 Lua 5.5 测试 (3次取平均) ---"
lua_total=0
for i in 1 2 3; do
    result=$(/Users/yun/git/lua/lua-5.5.0/src/lua bench/bench.lua 2>&1 | grep "total:" | awk '{print $2}')
    echo "  第$i次: $result ms"
    lua_total=$(echo "$lua_total + $result" | bc)
done
lua_avg=$(echo "scale=3; $lua_total / 3" | bc)
echo "  Lua 5.5 平均: $lua_avg ms"
echo ""

echo "--- 运行 Python3 测试 (3次取平均) ---"
py_total=0
for i in 1 2 3; do
    result=$(python3 -c "
import time

def add(a, b):
    return a + b

# 测试 1: 算术运算 (1000万次)
N1 = 10000000
start1 = time.perf_counter()
sum1 = 0
for i in range(1, N1 + 1):
    sum1 = sum1 + i
    if sum1 > 100000000:
        sum1 = sum1 - 100000000
t1 = (time.perf_counter() - start1) * 1000

# 测试 2: 循环 (1亿次)
N2 = 100000000
start2 = time.perf_counter()
sum2 = 0
for i in range(1, N2 + 1):
    sum2 = sum2 + i
t2 = (time.perf_counter() - start2) * 1000

# 测试 3: 函数调用 (5000万次)
N3 = 50000000
start3 = time.perf_counter()
sum3 = 0
for i in range(1, N3 + 1):
    sum3 = sum3 + add(i, 1)
t3 = (time.perf_counter() - start3) * 1000

# 测试 4: 简单循环 (200万次)
N4 = 2000000
start4 = time.perf_counter()
sum4 = 0
for i in range(1, N4 + 1):
    sum4 = sum4 + 1
t4 = (time.perf_counter() - start4) * 1000

total = t1 + t2 + t3 + t4
print(f'{total:.3f}')
" 2>&1)
    echo "  第$i次: $result ms"
    py_total=$(echo "$py_total + $result" | bc)
done
py_avg=$(echo "scale=3; $py_total / 3" | bc)
echo "  Python3 平均: $py_avg ms"
echo ""

echo "========================================="
echo "性能对比结果"
echo "========================================="
echo "Mos:     $mos_avg ms"
echo "Lua 5.5: $lua_avg ms"
echo "Python3: $py_avg ms"
echo ""

# 计算 Mos 相对于 Lua 的性能差异
if [ $(echo "$mos_avg < $lua_avg" | bc) -eq 1 ]; then
    mos_faster=$(echo "scale=1; ($lua_avg - $mos_avg) / $lua_avg * 100" | bc)
    echo "Mos 比 Lua 5.5 快 ${mos_faster}%"
else
    lua_faster=$(echo "scale=1; ($mos_avg - $lua_avg) / $lua_avg * 100" | bc)
    echo "Mos 比 Lua 5.5 慢 ${lua_faster}%"
fi

if [ $(echo "$mos_avg < $py_avg" | bc) -eq 1 ]; then
    mos_faster=$(echo "scale=1; ($py_avg - $mos_avg) / $py_avg * 100" | bc)
    echo "Mos 比 Python3 快 ${mos_faster}%"
else
    py_faster=$(echo "scale=1; ($mos_avg - $py_avg) / $py_avg * 100" | bc)
    echo "Mos 比 Python3 慢 ${py_faster}%"
fi
