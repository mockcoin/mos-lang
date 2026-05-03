#!/bin/bash
# Mos vs Lua 5.5 性能对比测试脚本

MOS="/Users/yun/git/lua/mos-src/mos"
LUA="/Users/yun/git/lua/lua-5.5.0/src/lua"
BENCH="/Users/yun/git/lua/mos-src/bench"

echo "========================================="
echo "Mos vs Lua 5.5 性能对比测试"
echo "========================================="
echo ""

# 测试 1: 算术运算
echo "--- 测试 1: 算术运算 (1000万次) ---"
echo -n "Mos:     "
$MOS $BENCH/bench_arith.mos 2>&1 | grep "time"
echo -n "Lua 5.5: "
$LUA $BENCH/bench_arith.lua 2>&1 | grep "time"
echo ""

# 测试 2: 循环
echo "--- 测试 2: 循环 (1亿次) ---"
echo -n "Mos:     "
$MOS $BENCH/bench_for_native.mos 2>&1 | grep "time"
echo -n "Lua 5.5: "
$LUA $BENCH/bench_for_lua.lua 2>&1 | grep "time"
echo ""

# 测试 3: 函数调用
echo "--- 测试 3: 函数调用 (5000万次) ---"
echo -n "Mos:     "
$MOS $BENCH/bench_funcall.mos 2>&1 | grep "time"
echo -n "Lua 5.5: "
$LUA $BENCH/bench_funcall_lua.lua 2>&1 | grep "time"
echo ""

# 测试 4: 斐波那契
echo "--- 测试 4: 斐波那契递归 (10次 fib(30)) ---"
echo -n "Mos:     "
$MOS $BENCH/bench_fib.mos 2>&1 | grep "time"
echo -n "Lua 5.5: "
$LUA $BENCH/bench_fib.lua 2>&1 | grep "time"
echo ""

# 测试 5: 简单循环
echo "--- 测试 5: 简单循环 (200万次) ---"
echo -n "Mos:     "
$MOS $BENCH/bench_loop.mos 2>&1 | grep "time"
echo -n "Lua 5.5: "
$LUA $BENCH/bench_loop.lua 2>&1 | grep "time"
echo ""
