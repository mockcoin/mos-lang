#ifndef MOS_API_H
#define MOS_API_H

#include "mos.h"
#include "mauxlib.h"

/*
 * Mos API - C/C++ 调用 VM 脚本函数的接口
 */

/* 调用 VM 脚本中的 @API 函数 */
LUALIB_API int mos_callAPI(lua_State *L, const char *funcname, int nargs, int nresults);

/* 便捷函数：调用返回字符串的 API 函数 */
LUALIB_API const char* mos_callAPI_string(lua_State *L, const char *funcname, int nargs);

/* 便捷函数：调用返回整数的 API 函数 */
LUALIB_API lua_Integer mos_callAPI_integer(lua_State *L, const char *funcname, int nargs);

/* 便捷函数：调用返回浮点数的 API 函数 */
LUALIB_API lua_Number mos_callAPI_number(lua_State *L, const char *funcname, int nargs);

#endif
