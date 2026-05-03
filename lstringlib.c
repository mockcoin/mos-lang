#include "mos.h"
#include "mauxlib.h"
#include "moslib.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/*
 * Mos String Library
 * 提供 String 类型的方法支持
 */

/* String.toUpperCase() - 转换为大写 */
static int string_toUpperCase(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t len = strlen(s);
  char *result = (char *)malloc(len + 1);
  for (size_t i = 0; i < len; i++) {
    result[i] = toupper((unsigned char)s[i]);
  }
  result[len] = '\0';
  lua_pushstring(L, result);
  free(result);
  return 1;
}

/* String.toLowerCase() - 转换为小写 */
static int string_toLowerCase(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t len = strlen(s);
  char *result = (char *)malloc(len + 1);
  for (size_t i = 0; i < len; i++) {
    result[i] = tolower((unsigned char)s[i]);
  }
  result[len] = '\0';
  lua_pushstring(L, result);
  free(result);
  return 1;
}

/* String.length() - 获取字符串长度 */
static int string_length(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  lua_pushinteger(L, (lua_Integer)strlen(s));
  return 1;
}

/* String.substring(start, end) - 获取子字符串 */
static int string_substring(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int start = (int)luaL_checkinteger(L, 2) - 1;  /* 1-based to 0-based */
  size_t len = strlen(s);
  
  if (start < 0) start = 0;
  if (start >= (int)len) {
    lua_pushstring(L, "");
    return 1;
  }
  
  int end;
  if (lua_isnoneornil(L, 3)) {
    end = (int)len;
  } else {
    end = (int)luaL_checkinteger(L, 3);
    if (end > (int)len) end = (int)len;
  }
  
  if (end <= start) {
    lua_pushstring(L, "");
    return 1;
  }
  
  lua_pushlstring(L, s + start, end - start);
  return 1;
}

/* String.indexOf(substring) - 查找子字符串位置 */
static int string_indexOf(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *sub = luaL_checkstring(L, 2);
  const char *pos = strstr(s, sub);
  
  if (pos == NULL) {
    lua_pushinteger(L, -1);
  } else {
    lua_pushinteger(L, (lua_Integer)(pos - s + 1));  /* 1-based */
  }
  return 1;
}

/* String.trim() - 去除两端空白 */
static int string_trim(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t len = strlen(s);
  
  /* Find start */
  size_t start = 0;
  while (start < len && isspace((unsigned char)s[start])) {
    start++;
  }
  
  /* Find end */
  size_t end = len;
  while (end > start && isspace((unsigned char)s[end - 1])) {
    end--;
  }
  
  lua_pushlstring(L, s + start, end - start);
  return 1;
}

/* String.startsWith(prefix) - 检查是否以指定字符串开头 */
static int string_startsWith(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *prefix = luaL_checkstring(L, 2);
  size_t s_len = strlen(s);
  size_t p_len = strlen(prefix);
  
  if (p_len > s_len) {
    lua_pushboolean(L, 0);
  } else {
    lua_pushboolean(L, strncmp(s, prefix, p_len) == 0);
  }
  return 1;
}

/* String.endsWith(suffix) - 检查是否以指定字符串结尾 */
static int string_endsWith(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *suffix = luaL_checkstring(L, 2);
  size_t s_len = strlen(s);
  size_t suf_len = strlen(suffix);
  
  if (suf_len > s_len) {
    lua_pushboolean(L, 0);
  } else {
    lua_pushboolean(L, strcmp(s + s_len - suf_len, suffix) == 0);
  }
  return 1;
}

/* String.split(separator) - 分割字符串 */
static int string_split(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *sep = luaL_optstring(L, 2, ",");
  size_t sep_len = strlen(sep);
  
  lua_newtable(L);
  int count = 1;
  
  const char *start = s;
  const char *pos = strstr(s, sep);
  
  while (pos != NULL) {
    lua_pushlstring(L, start, pos - start);
    lua_rawseti(L, -2, count++);
    start = pos + sep_len;
    pos = strstr(start, sep);
  }
  
  /* Add last part */
  lua_pushstring(L, start);
  lua_rawseti(L, -2, count);
  
  return 1;
}

/* String.replace(old, new) - 替换字符串 */
static int string_replace(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *old = luaL_checkstring(L, 2);
  const char *newstr = luaL_checkstring(L, 3);
  
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  const char *pos = strstr(s, old);
  if (pos == NULL) {
    lua_pushstring(L, s);
    return 1;
  }
  
  size_t old_len = strlen(old);
  const char *start = s;
  
  while (pos != NULL) {
    luaL_addlstring(&b, start, pos - start);
    luaL_addstring(&b, newstr);
    start = pos + old_len;
    pos = strstr(start, old);
  }
  
  luaL_addstring(&b, start);
  luaL_pushresult(&b);
  
  return 1;
}

/* String.charAt(index) - 获取指定位置的字符 */
static int string_charAt(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int index = (int)luaL_checkinteger(L, 2) - 1;  /* 1-based to 0-based */
  size_t len = strlen(s);
  
  if (index < 0 || index >= (int)len) {
    lua_pushstring(L, "");
  } else {
    char buf[2] = {s[index], '\0'};
    lua_pushstring(L, buf);
  }
  return 1;
}

/* String.repeat(count) - 重复字符串 */
static int string_repeatstr(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int count = (int)luaL_checkinteger(L, 2);
  
  if (count <= 0) {
    lua_pushstring(L, "");
    return 1;
  }
  
  size_t len = strlen(s);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  for (int i = 0; i < count; i++) {
    luaL_addlstring(&b, s, len);
  }
  
  luaL_pushresult(&b);
  return 1;
}

/* String methods table */
static const luaL_Reg string_methods[] = {
  {"toUpperCase", string_toUpperCase},
  {"toLowerCase", string_toLowerCase},
  {"length", string_length},
  {"substring", string_substring},
  {"indexOf", string_indexOf},
  {"trim", string_trim},
  {"startsWith", string_startsWith},
  {"endsWith", string_endsWith},
  {"split", string_split},
  {"replace", string_replace},
  {"charAt", string_charAt},
  {"repeat", string_repeatstr},
  {NULL, NULL}
};

/* Open string methods library */
LUAMOD_API int luaopen_stringmethods(lua_State *L) {
  luaL_newlib(L, string_methods);
  
  /* Set string library global for convenience */
  lua_pushvalue(L, -1);
  lua_setglobal(L, "String");
  
  return 1;
}

/* Initialize string methods for all strings (called during VM init) */
void luaS_initmethods(lua_State *L) {
  /* Create metatable for strings */
  luaL_newlib(L, string_methods);
  
  /* Store in registry for string type */
  lua_setfield(L, LUA_REGISTRYINDEX, "StringMethods");
}
