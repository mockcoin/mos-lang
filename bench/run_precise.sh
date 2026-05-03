#!/bin/bash
# 精确性能测试 - 每项运行3次取平均

MOS="/Users/yun/git/lua/mos-src/mos"
LUA="/Users/yun/git/lua/lua-5.5.0/src/lua"
BENCH="/Users/yun/git/lua/bench"

echo "========================================="
echo "Mos vs Lua 5.5 精确性能测试"
echo "========================================="
echo ""

# 测试 1: 算术运算 (简单)
echo "--- 算术运算 (1000万次) ---"
mos_times=()
lua_times=()
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_arith.mos 2>&1 | grep "time" | awk '{print $3}')
    lua_t=$($LUA $BENCH/bench_arith.lua 2>&1 | grep "time" | awk '{print $3}')
    mos_times+=($mos_t)
    lua_times+=($lua_t)
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
echo ""

# 测试 2: 算术运算 (复杂)
echo "--- 复杂算术运算 (1亿次) ---"
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_arith_typed.mos 2>&1 | grep "time" | awk '{print $3}')
    lua_t=$($LUA $BENCH/bench_arith_lua.lua 2>&1 | grep "time" | awk '{print $3}')
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
echo ""

# 测试 3: 循环
echo "--- 循环 (1亿次) ---"
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_for_native.mos 2>&1 | grep "time=" | awk '{print $NF}')
    lua_t=$($LUA $BENCH/bench_for_lua.lua 2>&1 | grep "time=" | awk '{print $NF}')
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
echo ""

# 测试 4: 函数调用
echo "--- 函数调用 (5000万次) ---"
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_funcall.mos 2>&1 | grep "time" | awk '{print $3}')
    lua_t=$($LUA $BENCH/bench_funcall_lua.lua 2>&1 | grep "time" | awk '{print $3}')
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
echo ""

# 测试 5: 简单循环
echo "--- 简单循环 (200万次) ---"
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_loop.mos 2>&1 | grep "time" | awk '{print $3}')
    lua_t=$($LUA $BENCH/bench_loop.lua 2>&1 | grep "time" | awk '{print $3}')
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
echo ""

# 测试 6: 斐波那契
echo "--- 斐波那契递归 (10次 fib(30)) ---"
for i in 1 2 3; do
    mos_t=$($MOS $BENCH/bench_fib.mos 2>&1 | grep "time" | awk '{print $3}')
    lua_t=$($LUA $BENCH/bench_fib.lua 2>&1 | grep "time" | awk '{print $3}')
    echo "  第$i次: Mos=$mos_t Lua=$lua_t"
done
