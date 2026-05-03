#include "mos.h"
#include "mauxlib.h"
#include "moslib.h"
#include <stdlib.h>
#include <string.h>

/*
 * Mos Array Library
 * 提供 Array 类型的方法支持
 */

/* Array.push(element) - 添加元素到末尾 */
static int array_push(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  lua_pushvalue(L, 2);
  lua_rawseti(L, 1, n + 1);
  lua_pushinteger(L, n + 1);
  return 1;
}

/* Array.pop() - 移除并返回末尾元素 */
static int array_pop(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  if (n == 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_rawgeti(L, 1, n);
  lua_pushnil(L);
  lua_rawseti(L, 1, n);
  return 1;
}

/* Array.shift() - 移除并返回第一个元素 */
static int array_shift(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  if (n == 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_rawgeti(L, 1, 1);
  for (int i = 1; i < n; i++) {
    lua_rawgeti(L, 1, i + 1);
    lua_rawseti(L, 1, i);
  }
  lua_pushnil(L);
  lua_rawseti(L, 1, n);
  return 1;
}

/* Array.unshift(element) - 添加元素到开头 */
static int array_unshift(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  for (int i = n; i >= 1; i--) {
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, 1, i + 1);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, 1, 1);
  lua_pushinteger(L, n + 1);
  return 1;
}

/* Array.length() - 获取数组长度 */
static int array_length(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushinteger(L, luaL_len(L, 1));
  return 1;
}

/* Array.forEach(callback) - 遍历数组 */
static int array_forEach(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int n = (int)luaL_len(L, 1);
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_rawgeti(L, 1, i);
    lua_pushinteger(L, i);
    lua_pushvalue(L, 1);
    lua_call(L, 3, 0);
  }
  return 0;
}

/* Array.map(callback) - 映射数组 */
static int array_map(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int n = (int)luaL_len(L, 1);
  lua_newtable(L);
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_rawgeti(L, 1, i);
    lua_pushinteger(L, i);
    lua_pushvalue(L, 1);
    lua_call(L, 3, 1);
    lua_rawseti(L, -2, i);
  }
  return 1;
}

/* Array.filter(callback) - 过滤数组 */
static int array_filter(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int n = (int)luaL_len(L, 1);
  lua_newtable(L);
  int j = 1;
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_rawgeti(L, 1, i);
    lua_pushinteger(L, i);
    lua_pushvalue(L, 1);
    lua_call(L, 3, 1);
    if (lua_toboolean(L, -1)) {
      lua_rawgeti(L, 1, i);
      lua_rawseti(L, -3, j++);
    }
    lua_pop(L, 1);
  }
  return 1;
}

/* Array.reduce(callback, initialValue) - 归约数组 */
static int array_reduce(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int n = (int)luaL_len(L, 1);
  int start = 2;
  if (lua_gettop(L) >= 3) {
    lua_pushvalue(L, 3);
    start = 1;
  } else {
    lua_rawgeti(L, 1, 1);
    start = 2;
  }
  for (int i = start; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -3);
    lua_rawgeti(L, 1, i);
    lua_pushinteger(L, i);
    lua_pushvalue(L, 1);
    lua_call(L, 4, 1);
    lua_remove(L, -2);
  }
  return 1;
}

/* Array.find(callback) - 查找元素 */
static int array_find(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int n = (int)luaL_len(L, 1);
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_rawgeti(L, 1, i);
    lua_pushinteger(L, i);
    lua_pushvalue(L, 1);
    lua_call(L, 3, 1);
    if (lua_toboolean(L, -1)) {
      lua_rawgeti(L, 1, i);
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pushnil(L);
  return 1;
}

/* Array.indexOf(element) - 查找元素索引 */
static int array_indexOf(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    if (lua_compare(L, -1, 2, LUA_OPEQ)) {
      lua_pushinteger(L, i);
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pushinteger(L, -1);
  return 1;
}

/* Array.includes(element) - 检查是否包含元素 */
static int array_includes(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    if (lua_compare(L, -1, 2, LUA_OPEQ)) {
      lua_pushboolean(L, 1);
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pushboolean(L, 0);
  return 1;
}

/* Array.slice(start, end) - 截取数组 */
static int array_slice(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  int start = (int)luaL_optinteger(L, 2, 1);
  int end = (int)luaL_optinteger(L, 3, n);
  if (start < 1) start = n + start + 1;
  if (end < 0) end = n + end + 1;
  if (start > end || start > n) {
    lua_newtable(L);
    return 1;
  }
  lua_newtable(L);
  int j = 1;
  for (int i = start; i <= end && i <= n; i++) {
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, -2, j++);
  }
  return 1;
}

/* Array.splice(start, count) - 删除并插入元素 */
static int array_splice(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  int start = (int)luaL_checkinteger(L, 2);
  int count = (int)luaL_optinteger(L, 3, n - start + 1);
  lua_newtable(L);
  int result_idx = 1;
  for (int i = start; i < start + count && i <= n; i++) {
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, -2, result_idx++);
  }
  for (int i = start + count; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, 1, i - count);
  }
  int new_args = lua_gettop(L) - 3;
  for (int i = 0; i < new_args; i++) {
    lua_rawgeti(L, 1, start + i);
    lua_pushvalue(L, 4 + i);
    lua_rawseti(L, 1, start + i);
  }
  for (int i = n - count + new_args + 1; i <= n; i++) {
    lua_pushnil(L);
    lua_rawseti(L, 1, i);
  }
  return 1;
}

/* Array.concat(other) - 连接数组 */
static int array_concat(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TTABLE);
  int n1 = (int)luaL_len(L, 1);
  int n2 = (int)luaL_len(L, 2);
  lua_newtable(L);
  for (int i = 1; i <= n1; i++) {
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, -2, i);
  }
  for (int i = 1; i <= n2; i++) {
    lua_rawgeti(L, 2, i);
    lua_rawseti(L, -2, n1 + i);
  }
  return 1;
}

/* Array.join(separator) - 连接为字符串 */
static int array_join(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  const char *sep = luaL_optstring(L, 2, ",");
  int n = (int)luaL_len(L, 1);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (int i = 1; i <= n; i++) {
    if (i > 1) luaL_addstring(&b, sep);
    lua_rawgeti(L, 1, i);
    if (lua_isstring(L, -1)) {
      size_t len;
      const char *s = lua_tolstring(L, -1, &len);
      luaL_addlstring(&b, s, len);
    }
    lua_pop(L, 1);
  }
  luaL_pushresult(&b);
  return 1;
}

/* Array.reverse() - 反转数组 */
static int array_reverse(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  for (int i = 1; i <= n / 2; i++) {
    lua_rawgeti(L, 1, i);
    lua_rawgeti(L, 1, n - i + 1);
    lua_rawseti(L, 1, i);
    lua_pushvalue(L, -1);
    lua_rawseti(L, 1, n - i + 1);
    lua_pop(L, 1);
  }
  lua_pushvalue(L, 1);
  return 1;
}

/* Array.sort() - 排序数组 */
static int array_sort(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = (int)luaL_len(L, 1);
  if (lua_gettop(L) >= 2) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 0);
  } else {
    lua_pushvalue(L, 1);
    lua_call(L, 1, 0);
  }
  lua_pushvalue(L, 1);
  return 1;
}

/* Array methods table */
static const luaL_Reg array_methods[] = {
  {"push", array_push},
  {"pop", array_pop},
  {"shift", array_shift},
  {"unshift", array_unshift},
  {"length", array_length},
  {"forEach", array_forEach},
  {"map", array_map},
  {"filter", array_filter},
  {"reduce", array_reduce},
  {"find", array_find},
  {"indexOf", array_indexOf},
  {"includes", array_includes},
  {"slice", array_slice},
  {"splice", array_splice},
  {"concat", array_concat},
  {"join", array_join},
  {"reverse", array_reverse},
  {"sort", array_sort},
  {NULL, NULL}
};

/* Open array methods library */
LUAMOD_API int luaopen_arraymethods(lua_State *L) {
  luaL_newlib(L, array_methods);
  lua_setglobal(L, "Array");
  return 1;
}
