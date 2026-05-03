#!/usr/bin/env python3
# Python 3 性能基准测试

import time

# 测试 1: 算术运算 (1000万次)
def bench_arith():
    N = 10000000
    start = time.perf_counter()
    sum_val = 0
    for i in range(1, N + 1):
        sum_val = sum_val + i
        if sum_val > 100000000:
            sum_val = sum_val - 100000000
    elapsed = time.perf_counter() - start
    print(f"sum = {sum_val}")
    print(f"time = {elapsed} seconds")

# 测试 2: 复杂算术运算 (1亿次)
def bench_arith_complex():
    N = 100000000
    a = 0
    b = 1
    c = 2
    start = time.perf_counter()
    for i in range(1, N + 1):
        a = a + b * c
    elapsed = time.perf_counter() - start
    print(f"arith: a={a} time={elapsed}")

# 测试 3: 循环 (1亿次)
def bench_loop():
    N = 100000000
    sum_val = 0
    start = time.perf_counter()
    for i in range(1, N + 1):
        sum_val = sum_val + i
    elapsed = time.perf_counter() - start
    print(f"sum={sum_val} time={elapsed}")

# 测试 4: 函数调用 (5000万次)
def add(a, b):
    return a + b

def bench_funcall():
    N = 50000000
    sum_val = 0
    start = time.perf_counter()
    for i in range(1, N + 1):
        sum_val = sum_val + add(i, 1)
    elapsed = time.perf_counter() - start
    print(f"sum = {sum_val}")
    print(f"time = {elapsed} seconds")

# 测试 5: 简单循环 (200万次)
def bench_simple_loop():
    N = 2000000
    sum_val = 0
    start = time.perf_counter()
    for i in range(1, N + 1):
        sum_val = sum_val + 1
    elapsed = time.perf_counter() - start
    print(f"sum = {sum_val}")
    print(f"time = {elapsed} seconds")

# 测试 6: 斐波那契递归
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

def bench_fib():
    N = 10
    result = 0
    start = time.perf_counter()
    for i in range(1, N + 1):
        result = result + fib(30)
    elapsed = time.perf_counter() - start
    print(f"result = {result}")
    print(f"time = {elapsed} seconds")

if __name__ == "__main__":
    print("=== 测试 1: 算术运算 (1000万次) ===")
    bench_arith()
    print()
    
    print("=== 测试 2: 复杂算术运算 (1亿次) ===")
    bench_arith_complex()
    print()
    
    print("=== 测试 3: 循环 (1亿次) ===")
    bench_loop()
    print()
    
    print("=== 测试 4: 函数调用 (5000万次) ===")
    bench_funcall()
    print()
    
    print("=== 测试 5: 简单循环 (200万次) ===")
    bench_simple_loop()
    print()
    
    print("=== 测试 6: 斐波那契递归 (10次 fib(30)) ===")
    bench_fib()
