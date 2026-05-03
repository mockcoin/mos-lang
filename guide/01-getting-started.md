# Mos 语言入门指南

Mos 是一门轻量级脚本语言，采用 C 风格语法，支持面向对象编程和丰富的内置库。

## 1. 快速开始

### 1.1 编译 Mos

```bash
cd mos-src
make
```

### 1.2 运行第一个程序

创建文件 `hello.mos`：

```mos
print("Hello, Mos!")
```

运行：

```bash
./mos-src/mos hello.mos
```

输出：
```
Hello, Mos!
```

### 1.3 变量

Mos 是强类型、静态类型语言，变量声明必须包含类型关键字。

```mos
// 变量声明必须指定类型
int x = 10
int y = 20
int z = x + y
print("x + y =", z)

String name = "Mos"
print("Hello, " .. name)
```

### 1.4 函数

```mos
// 参数和返回值都需要类型声明，使用大括号包裹函数体
int add(int a, int b) {
    return a + b
}

String greet(String name) {
    return "Hello, " .. name
}

int result = add(10, 20)
print("add(10, 20) =", result)

String msg = greet("World")
print(msg)
```

## 2. 语言特性概览

| 特性 | 语法 |
|------|------|
| 变量 | `int x = 10` |
| 函数 | `int foo(int a) { ... }` |
| 条件 | `if (cond) { ... } else { ... }` |
| 循环 | `for (int i = 0; i < 10; i = i + 1) { ... }` |
| 类 | `class Name { ... }` |
| 对象 | `obj = new Class()` |
| 方法调用 | `obj:method()` |
| 匿名函数 | `function(int x) { ... }` |
| 字符串连接 | `..` |
| 注释 | `// 单行注释` |

## 3. 下一步

- [语言规范](02-language-spec.md) - 详细的语法规则
- [标准库](03-standard-library.md) - Map、Array、String 等内置库
- [代码示例](04-examples.md) - 完整的示例程序
