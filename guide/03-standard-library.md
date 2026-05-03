# Mos 标准库

Mos 提供了丰富的标准库，包括 Map、Array 和 String 等常用数据结构和方法。

## 3.1 Map 库

Map 是一个键值对集合，支持多种数据类型作为键和值。

### 3.1.1 创建 Map

```mos
Map = require "Map"

// 创建空 Map
m = Map.new()

// 从 JSON 字符串创建 Map
m2 = Map.decode('{"name": "Mos", "version": 1}')
```

### 3.1.2 Map 方法

| 方法 | 说明 | 示例 |
|------|------|------|
| `set(key, value)` | 设置键值对 | `m:set("name", "Mos")` |
| `get(key)` | 获取键对应的值 | `m:get("name")` |
| `has(key)` | 检查键是否存在 | `m:has("name")` |
| `delete(key)` | 删除键值对 | `m:delete("name")` |
| `size()` | 返回键值对数量 | `m:size()` |
| `clear()` | 清空所有键值对 | `m:clear()` |
| `forEach(callback)` | 遍历所有键值对 | `m:forEach(function(k, v) { ... })` |
| `keys()` | 返回所有键 | `m:keys()` |
| `values()` | 返回所有值 | `m:values()` |
| `entries()` | 返回所有键值对 | `m:entries()` |
| `merge(otherMap)` | 合并另一个 Map | `m:merge(otherMap)` |
| `decode(jsonStr)` | 从 JSON 字符串解析 | `Map.decode('{"a": 1}')` |
| `toJsonString()` | 转换为 JSON 字符串 | `m:toJsonString()` |

### 3.1.3 使用示例

```mos
Map = require "Map"

// 创建并设置键值对
m = Map.new()
m:set("name", "Mos")
m:set("version", 1)
m:set("active", true)

// 获取值
print(m:get("name"))  // 输出: Mos

// 检查键是否存在
print(m:has("name"))  // 输出: true

// 删除键值对
m:delete("active")

// 获取大小
print(m:size())  // 输出: 2

// 遍历
m:forEach(function(k, v) {
    print(k, "=", v)
})

// 转换为 JSON
print(m:toJsonString())  // 输出: {"name":"Mos","version":1}
```

## 3.2 Array 库

Array 是一个有序集合，支持多种操作方法。

### 3.2.1 创建 Array

```mos
Array = require "Array"

// 创建空 Array
a = Array.new()

// 从表创建 Array
a2 = Array.from({1, 2, 3})
```

### 3.2.2 Array 方法

| 方法 | 说明 | 示例 |
|------|------|------|
| `push(value)` | 在末尾添加元素 | `a:push(10)` |
| `pop()` | 移除并返回末尾元素 | `a:pop()` |
| `shift()` | 移除并返回第一个元素 | `a:shift()` |
| `unshift(value)` | 在开头添加元素 | `a:unshift(0)` |
| `length()` | 返回数组长度 | `a:length()` |
| `forEach(callback)` | 遍历所有元素 | `a:forEach(function(v) { ... })` |
| `map(callback)` | 映射每个元素 | `a:map(function(v) { return v * 2 })` |
| `filter(callback)` | 过滤元素 | `a:filter(function(v) { return v > 0 })` |
| `reduce(callback, initial)` | 归约数组 | `a:reduce(function(acc, v) { return acc + v }, 0)` |
| `find(callback)` | 查找第一个匹配元素 | `a:find(function(v) { return v > 5 })` |
| `indexOf(value)` | 查找元素索引 | `a:indexOf(10)` |
| `includes(value)` | 检查是否包含元素 | `a:includes(10)` |
| `slice(start, end)` | 截取子数组 | `a:slice(0, 3)` |
| `splice(start, count)` | 删除/插入元素 | `a:splice(1, 2)` |
| `concat(other)` | 连接数组 | `a:concat(b)` |
| `join(separator)` | 连接为字符串 | `a:join(",")` |
| `reverse()` | 反转数组 | `a:reverse()` |
| `sort()` | 排序数组 | `a:sort()` |

### 3.2.3 使用示例

```mos
Array = require "Array"

// 创建并添加元素
a = Array.new()
a:push(1)
a:push(2)
a:push(3)

// 获取长度
print(a:length())  // 输出: 3

// 遍历
a:forEach(function(v) {
    print(v)
})

// 映射
b = a:map(function(v) {
    return v * 2
})

// 过滤
c = a:filter(function(v) {
    return v > 1
})

// 归约
sum = a:reduce(function(acc, v) {
    return acc + v
}, 0)
print("sum =", sum)  // 输出: sum = 6
```

## 3.3 String 库

String 库提供了字符串操作方法。

### 3.3.1 String 方法

| 方法 | 说明 | 示例 |
|------|------|------|
| `toUpperCase()` | 转换为大写 | `s:toUpperCase()` |
| `toLowerCase()` | 转换为小写 | `s:toLowerCase()` |
| `length()` | 返回字符串长度 | `s:length()` |
| `substring(start, end)` | 截取子串 | `s:substring(0, 5)` |
| `indexOf(substr)` | 查找子串位置 | `s:indexOf("Mos")` |
| `trim()` | 去除两端空白 | `s:trim()` |
| `startsWith(prefix)` | 检查是否以某串开头 | `s:startsWith("Hello")` |
| `endsWith(suffix)` | 检查是否以某串结尾 | `s:endsWith("World")` |
| `split(separator)` | 分割为数组 | `s:split(",")` |
| `replace(old, new)` | 替换子串 | `s:replace("Mos", "Language")` |
| `charAt(index)` | 获取指定位置字符 | `s:charAt(0)` |
| `repeat(count)` | 重复字符串 | `s:repeat(3)` |

### 3.3.2 使用示例

```mos
String = require "String"

s = "Hello, Mos!"

// 转换为大写
print(s:toUpperCase())  // 输出: HELLO, MOS!

// 获取长度
print(s:length())  // 输出: 11

// 截取子串
print(s:substring(0, 5))  // 输出: Hello

// 查找子串
print(s:indexOf("Mos"))  // 输出: 7

// 替换
print(s:replace("Mos", "World"))  // 输出: Hello, World!

// 分割
parts = s:split(", ")
print(parts[0])  // 输出: Hello
print(parts[1])  // 输出: Mos!
```

## 3.4 内置函数

Mos 提供了一些全局内置函数。

| 函数 | 说明 | 示例 |
|------|------|------|
| `print(...)` | 打印输出 | `print("Hello")` |
| `type(value)` | 返回值的类型 | `type(10)` → "number" |
| `tostring(value)` | 转换为字符串 | `tostring(10)` → "10" |
| `tonumber(value)` | 转换为数字 | `tonumber("10")` → 10 |

### 3.4.1 print 函数

```mos
// 打印单个值
print("Hello")

// 打印多个值
print("x =", 10, "y =", 20)

// 打印表达式结果
print("sum =", 10 + 20)
```

## 3.5 导入库

使用 `require` 导入标准库：

```mos
Map = require "Map"
Array = require "Array"
String = require "String"
```
