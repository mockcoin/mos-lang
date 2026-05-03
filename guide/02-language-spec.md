# Mos 语言规范

## 2.1 词法元素

### 2.1.1 注释

Mos 采用 C 语言标准的注释风格：

**单行注释**：以 `//` 开始，到行尾结束。

```mos
// 这是单行注释
int x = 10  // 这也是注释
```

**多行注释**：以 `/*` 开始，以 `*/` 结束。

```mos
/*
这是多行注释
可以跨越多行
*/
int y = 20
```

### 2.1.2 标识符

标识符由字母、数字和下划线组成，不能以数字开头。

```mos
name = "Mos"
count = 100
my_var = true
_private = 42
```

### 2.1.3 关键字

```
and       break     case      default   do        else
elseif    end       false     for       function  global
goto      if        in        new       nil       not
or        repeat    return    switch    then      true
until     while     api       class     try       catch
import    export
```

### 2.1.4 字面量

**整数**：十进制或十六进制。

```mos
a = 10
b = -5
c = 0xFF
d = 0x1A
```

**浮点数**：

```mos
pi = 3.14
e = 2.718
small = 0.001
```

**字符串**：双引号或单引号。

```mos
s1 = "Hello"
s2 = 'World'
```

**布尔值**：

```mos
flag = true
disabled = false
```

**nil**：

```mos
value = nil
```

## 2.2 变量

### 2.2.1 变量声明

Mos 是强类型、静态类型语言。变量声明必须包含类型关键字。

```mos
int x = 10
float pi = 3.14
String name = "Mos"
bool flag = true
```

### 2.2.2 类型系统

| 类型 | 说明 | 示例 |
|------|------|------|
| `int` | 整数 | `10`, `-5`, `0xFF` |
| `float` | 浮点数 | `3.14`, `2.718` |
| `String` | 字符串 | `"Hello"`, `'World'` |
| `bool` | 布尔值 | `true`, `false` |

### 2.2.3 作用域

```mos
// 全局变量
int globalVar = 100

int main() {
    // 局部变量
    int localVar = 200
    print(localVar)
    return 0
}
```

## 2.3 运算符

### 2.3.1 算术运算符

```mos
int a = 10
int b = 3

int sum = a + b       // 加法: 13
int diff = a - b      // 减法: 7
int product = a * b   // 乘法: 30
int quotient = a / b  // 整数除法: 3 (截断小数部分)
int remainder = a % b // 取模: 1
```

**整数除法说明**：

Mos 遵循 C 语言的除法语则：
- `int / int` → 整数除法，结果截断小数部分
- `float / float` → 浮点除法
- `int / float` 或 `float / int` → 浮点除法（类型提升）

```mos
int a = 10
int b = 3
float fa = 10.0
float fb = 3.0

int r1 = a / b      // 3 (整数除法)
float r2 = fa / fb  // 3.333... (浮点除法)
float r3 = a / fb   // 3.333... (类型提升为浮点)
```

### 2.3.2 比较运算符

```mos
bool eq = (a == b)   // 等于
bool ne = (a ~= b)   // 不等于
bool gt = (a > b)    // 大于
bool lt = (a < b)    // 小于
bool ge = (a >= b)   // 大于等于
bool le = (a <= b)   // 小于等于
```

### 2.3.3 逻辑运算符

```mos
bool p = true
bool q = false

bool r1 = p and q   // 逻辑与: false
bool r2 = p or q    // 逻辑或: true
bool r3 = not p     // 逻辑非: false
```

### 2.3.4 字符串连接

```mos
String s1 = "Hello"
String s2 = "World"
String s3 = s1 .. " " .. s2  // "Hello World"
```

### 2.3.5 位运算符

```mos
int a = 0xFF
int b = 0x0F

int r1 = a & b   // 按位与
int r2 = a | b   // 按位或
int r3 = a ~ b   // 按位异或
int r4 = a << 2  // 左移
int r5 = a >> 2  // 右移
```

## 2.4 控制流

### 2.4.1 条件语句

```mos
int x = 10

if (x > 0) {
    print("positive")
} else {
    print("not positive")
}
```

### 2.4.2 while 循环

```mos
int i = 0
while (i < 5) {
    print(i)
    i = i + 1
}
```

### 2.4.3 for 循环

```mos
// C 风格 for 循环
for (int i = 0; i < 5; i = i + 1) {
    print(i)
}
```

### 2.4.4 break 语句

```mos
for (int i = 0; i < 100; i = i + 1) {
    if (i == 5) {
        break
    }
    print(i)
}
```

### 2.4.5 switch 语句

```mos
int day = 1

switch (day) {
    case 1:
        print("Monday")
        break
    case 2:
        print("Tuesday")
        break
    default:
        print("Other")
}
```

## 2.5 函数

### 2.5.1 函数定义

```mos
int add(int a, int b) {
    return a + b
}

void printHello() {
    print("Hello!")
}

String greet(String name) {
    return "Hello, " .. name
}
```

### 2.5.2 函数调用

```mos
int result = add(10, 20)
printHello()
String msg = greet("World")
```

### 2.5.3 匿名函数

```mos
// 匿名函数定义
f = function(int x) {
    return x * 2
}

int result = f(5)
print("result =", result)  // 输出: result = 10
```

**注意**：当前版本中，匿名函数可以作为变量赋值，但作为函数参数传递时，参数类型声明暂不支持 `function` 类型。

### 2.5.4 @API 注解

```mos
@API
int add(int a, int b) {
    return a + b
}
```

## 2.6 类和对象

### 2.6.1 类定义

```mos
class Person {
    String name
    int age
    
    void init(String n, int a) {
        self.name = n
        self.age = a
    }
    
    String getName() {
        return self.name
    }
    
    int getAge() {
        return self.age
    }
}
```

### 2.6.2 对象创建

```mos
Person p = new Person("Alice", 30)
```

### 2.6.3 方法调用

```mos
String name = p:getName()
int age = p:getAge()
```

### 2.6.4 self 引用

```mos
class Counter {
    int count
    
    void init() {
        self.count = 0
    }
    
    void increment() {
        self.count = self.count + 1
    }
    
    int getCount() {
        return self.count
    }
}

Counter c = new Counter()
c:increment()
c:increment()
print("count =", c:getCount())
```

## 2.7 异常处理

### 2.7.1 try-catch

```mos
try {
    print("trying...")
} catch (e) {
    print("Error:", e)
}
```

## 2.8 模块

### 2.8.1 require 导入

```mos
Map = require "Map"
```

### 2.8.2 export 导出

```mos
@API
int add(int a, int b) {
    return a + b
}
```
