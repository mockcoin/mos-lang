#include "mos.h"
#include "mauxlib.h"
#include "moslib.h"
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

/*
 * Mos Map/Dictionary Library
 * 提供 Map 类型的方法支持 + JSON 功能
 */

/* ========== Map Methods ========== */

/* Map.set(key, value) - 设置键值对 */
static int map_set(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_settable(L, 1);
  return 0;
}

/* Map.get(key) - 获取值 */
static int map_get(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_gettable(L, 1);
  return 1;
}

/* Map.has(key) - 检查键是否存在 */
static int map_has(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_gettable(L, 1);
  lua_pushboolean(L, !lua_isnil(L, -1));
  return 1;
}

/* Map.delete(key) - 删除键值对 */
static int map_delete(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_pushnil(L);
  lua_settable(L, 1);
  return 0;
}

/* Map.size() - 获取大小 */
static int map_size(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int count = 0;
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    count++;
    lua_pop(L, 1);
  }
  lua_pushinteger(L, count);
  return 1;
}

/* Map.clear() - 清空 Map */
static int map_clear(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, -2);
    lua_pushnil(L);
    lua_settable(L, 1);
    lua_pop(L, 1);
  }
  return 0;
}

/* Map.forEach(callback) - 遍历 Map */
static int map_forEach(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -3);
    lua_pushvalue(L, -3);
    lua_pushvalue(L, 1);
    lua_call(L, 3, 0);
    lua_pop(L, 1);
  }
  return 0;
}

/* Map.keys() - 获取所有键 */
static int map_keys(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_newtable(L);
  int i = 1;
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, -2);
    lua_rawseti(L, -4, i++);
    lua_pop(L, 1);
  }
  return 1;
}

/* Map.values() - 获取所有值 */
static int map_values(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_newtable(L);
  int i = 1;
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, -1);
    lua_rawseti(L, -4, i++);
    lua_pop(L, 1);
  }
  return 1;
}

/* Map.entries() - 获取所有键值对 */
static int map_entries(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_newtable(L);
  int i = 1;
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_newtable(L);
    lua_pushvalue(L, -3);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, -3);
    lua_rawseti(L, -2, 2);
    lua_rawseti(L, -4, i++);
    lua_pop(L, 1);
  }
  return 1;
}

/* Map.merge(other) - 合并 Map */
static int map_merge(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_newtable(L);
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_settable(L, -5);
    lua_pop(L, 1);
  }
  lua_pushnil(L);
  while (lua_next(L, 2) != 0) {
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_settable(L, -5);
    lua_pop(L, 1);
  }
  return 1;
}

/* ========== JSON Methods ========== */

/* Forward declarations for JSON */
static void json_encode(lua_State *L, int idx, luaL_Buffer *b, int depth);

/* Skip whitespace */
static const char *json_skip(const char *s) {
  while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
    s++;
  return s;
}

/* Parse JSON string value */
static const char *json_parse_string(const char *s, luaL_Buffer *b) {
  if (*s != '"') return NULL;
  s++;
  while (*s && *s != '"') {
    if (*s == '\\') {
      s++;
      switch (*s) {
        case '"': luaL_addchar(b, '"'); break;
        case '\\': luaL_addchar(b, '\\'); break;
        case '/': luaL_addchar(b, '/'); break;
        case 'n': luaL_addchar(b, '\n'); break;
        case 'r': luaL_addchar(b, '\r'); break;
        case 't': luaL_addchar(b, '\t'); break;
        case 'b': luaL_addchar(b, '\b'); break;
        case 'f': luaL_addchar(b, '\f'); break;
        default: luaL_addchar(b, '\\'); luaL_addchar(b, *s); break;
      }
    } else {
      luaL_addchar(b, *s);
    }
    s++;
  }
  if (*s == '"') s++;
  return s;
}

/* Parse JSON number */
static const char *json_parse_number(const char *s, lua_Number *n) {
  char *end;
  *n = strtod(s, &end);
  if (end == s) return NULL;
  return end;
}

/* Parse JSON value */
static const char *json_parse_value(lua_State *L, const char *s);

/* Parse JSON object */
static const char *json_parse_object(lua_State *L, const char *s) {
  lua_newtable(L);
  s = json_skip(s);
  if (*s == '}') return s + 1;
  
  while (*s) {
    luaL_Buffer keybuf;
    luaL_buffinit(L, &keybuf);
    s = json_parse_string(s, &keybuf);
    if (!s) return NULL;
    luaL_pushresult(&keybuf);
    
    s = json_skip(s);
    if (*s != ':') return NULL;
    s++;
    
    s = json_parse_value(L, json_skip(s));
    if (!s) return NULL;
    
    lua_settable(L, -3);
    
    s = json_skip(s);
    if (*s == '}') return s + 1;
    if (*s != ',') return NULL;
    s++;
    s = json_skip(s);
  }
  return NULL;
}

/* Parse JSON array */
static const char *json_parse_array(lua_State *L, const char *s) {
  int idx = 1;
  lua_newtable(L);
  s = json_skip(s);
  if (*s == ']') return s + 1;
  
  while (*s) {
    s = json_parse_value(L, json_skip(s));
    if (!s) return NULL;
    lua_rawseti(L, -2, idx++);
    
    s = json_skip(s);
    if (*s == ']') return s + 1;
    if (*s != ',') return NULL;
    s++;
  }
  return NULL;
}

/* Parse JSON value */
static const char *json_parse_value(lua_State *L, const char *s) {
  s = json_skip(s);
  
  if (*s == '"') {
    luaL_Buffer buf;
    luaL_buffinit(L, &buf);
    s = json_parse_string(s, &buf);
    if (!s) return NULL;
    luaL_pushresult(&buf);
  }
  else if (*s == '{') {
    s = json_parse_object(L, s + 1);
    if (!s) return NULL;
  }
  else if (*s == '[') {
    s = json_parse_array(L, s + 1);
    if (!s) return NULL;
  }
  else if (strncmp(s, "true", 4) == 0) {
    lua_pushboolean(L, 1);
    s += 4;
  }
  else if (strncmp(s, "false", 5) == 0) {
    lua_pushboolean(L, 0);
    s += 5;
  }
  else if (strncmp(s, "null", 4) == 0) {
    lua_pushnil(L);
    s += 4;
  }
  else if (*s == '-' || isdigit(*s)) {
    lua_Number n;
    s = json_parse_number(s, &n);
    if (!s) return NULL;
    lua_pushnumber(L, n);
  }
  else {
    return NULL;
  }
  
  return s;
}

/* Map.decode(jsonString) - 解析 JSON 字符串为 table */
static int map_decode(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *end;
  
  s = json_skip(s);
  end = json_parse_value(L, s);
  
  if (!end) {
    lua_pushnil(L);
    lua_pushstring(L, "Invalid JSON");
    return 2;
  }
  
  return 1;
}

/* Encode string with escaping */
static void json_encode_string(luaL_Buffer *b, const char *s, size_t len) {
  luaL_addchar(b, '"');
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    switch (c) {
      case '"':  luaL_addchar(b, '\\'); luaL_addchar(b, '"'); break;
      case '\\': luaL_addchar(b, '\\'); luaL_addchar(b, '\\'); break;
      case '\n': luaL_addchar(b, '\\'); luaL_addchar(b, 'n'); break;
      case '\r': luaL_addchar(b, '\\'); luaL_addchar(b, 'r'); break;
      case '\t': luaL_addchar(b, '\\'); luaL_addchar(b, 't'); break;
      case '\b': luaL_addchar(b, '\\'); luaL_addchar(b, 'b'); break;
      case '\f': luaL_addchar(b, '\\'); luaL_addchar(b, 'f'); break;
      default:
        if (iscntrl(c)) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          luaL_addstring(b, buf);
        } else {
          luaL_addchar(b, c);
        }
        break;
    }
  }
  luaL_addchar(b, '"');
}

/* JSON encode function */
static void json_encode(lua_State *L, int idx, luaL_Buffer *b, int depth) {
  int t = lua_type(L, idx);
  
  switch (t) {
    case LUA_TNIL:
      luaL_addstring(b, "null");
      break;
      
    case LUA_TBOOLEAN:
      luaL_addstring(b, lua_toboolean(L, idx) ? "true" : "false");
      break;
      
    case LUA_TNUMBER:
      if (lua_isinteger(L, idx)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, idx));
        luaL_addstring(b, buf);
      } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.14g", lua_tonumber(L, idx));
        luaL_addstring(b, buf);
      }
      break;
      
    case LUA_TSTRING: {
      size_t len;
      const char *s = lua_tolstring(L, idx, &len);
      json_encode_string(b, s, len);
      break;
    }
      
    case LUA_TTABLE: {
      int is_array = 1;
      int len = 0;
      int max_key = 0;
      int abs_idx = lua_absindex(L, idx);
      
      lua_pushnil(L);
      while (lua_next(L, abs_idx) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER) {
          int key = lua_tointeger(L, -2);
          if (key > max_key) max_key = key;
        } else {
          is_array = 0;
        }
        lua_pop(L, 1);
        len++;
      }
      
      if (is_array && len > 0 && max_key != len) {
        is_array = 0;
      }
      
      if (is_array && len > 0) {
        luaL_addchar(b, '[');
        for (int i = 1; i <= len; i++) {
          if (i > 1) luaL_addchar(b, ',');
          lua_rawgeti(L, abs_idx, i);
          json_encode(L, -1, b, depth + 1);
          lua_pop(L, 1);
        }
        luaL_addchar(b, ']');
      } else {
        int first = 1;
        luaL_addchar(b, '{');
        lua_pushnil(L);
        while (lua_next(L, abs_idx) != 0) {
          if (!first) luaL_addchar(b, ',');
          if (lua_type(L, -2) == LUA_TSTRING) {
            size_t klen;
            const char *k = lua_tolstring(L, -2, &klen);
            json_encode_string(b, k, klen);
          } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -2));
            json_encode_string(b, buf, strlen(buf));
          }
          luaL_addchar(b, ':');
          json_encode(L, -1, b, depth + 1);
          lua_pop(L, 1);
          first = 0;
        }
        luaL_addchar(b, '}');
      }
      break;
    }
      
    default:
      luaL_addstring(b, "null");
      break;
  }
}

/* Map.toJsonString() - 将 table 编码为 JSON 字符串 */
static int map_toJsonString(lua_State *L) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  json_encode(L, 1, &b, 0);
  
  luaL_pushresult(&b);
  return 1;
}

/* Map methods table */
static const luaL_Reg map_methods[] = {
  {"set", map_set},
  {"get", map_get},
  {"has", map_has},
  {"delete", map_delete},
  {"size", map_size},
  {"clear", map_clear},
  {"forEach", map_forEach},
  {"keys", map_keys},
  {"values", map_values},
  {"entries", map_entries},
  {"merge", map_merge},
  {"decode", map_decode},
  {"toJsonString", map_toJsonString},
  {NULL, NULL}
};

/* Open map methods library */
LUAMOD_API int luaopen_mapmethods(lua_State *L) {
  luaL_newlib(L, map_methods);
  lua_setglobal(L, "Map");
  return 1;
}
