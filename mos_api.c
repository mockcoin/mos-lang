#include "mos.h"
#include "mauxlib.h"
#include "moslib.h"
#include "mos_api.h"

/*
 * Mos API Implementation
 * C/C++ 调用 VM 脚本函数的接口
 */

/* 调用 VM 脚本中的 @API 函数 */
LUALIB_API int mos_callAPI(lua_State *L, const char *funcname, int nargs, int nresults) {
    /* 获取全局函数 */
    lua_getglobal(L, funcname);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "function '%s' not found or not marked as @API", funcname);
        return 0;
    }
    
    /* 函数在栈顶，参数在函数下面，需要重新排列栈 */
    /* 当前栈：... func */
    /* 需要将函数移到参数下面 */
    lua_insert(L, -(nargs + 1));
    
    /* 调用函数 */
    if (lua_pcall(L, nargs, nresults, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        luaL_error(L, "error calling API function '%s': %s", funcname, err);
        return 0;
    }
    
    /* 返回结果数量 */
    return nresults;
}

/* 便捷函数：调用返回字符串的 API 函数 */
LUALIB_API const char* mos_callAPI_string(lua_State *L, const char *funcname, int nargs) {
    int nresults = mos_callAPI(L, funcname, nargs, 1);
    if (nresults == 0) {
        return NULL;
    }
    
    if (!lua_isstring(L, -1)) {
        luaL_error(L, "API function '%s' did not return a string", funcname);
        return NULL;
    }
    
    return lua_tostring(L, -1);
}

/* 便捷函数：调用返回整数的 API 函数 */
LUALIB_API lua_Integer mos_callAPI_integer(lua_State *L, const char *funcname, int nargs) {
    int nresults = mos_callAPI(L, funcname, nargs, 1);
    if (nresults == 0) {
        return 0;
    }
    
    if (!lua_isinteger(L, -1)) {
        luaL_error(L, "API function '%s' did not return an integer", funcname);
        return 0;
    }
    
    return lua_tointeger(L, -1);
}

/* 便捷函数：调用返回浮点数的 API 函数 */
LUALIB_API lua_Number mos_callAPI_number(lua_State *L, const char *funcname, int nargs) {
    int nresults = mos_callAPI(L, funcname, nargs, 1);
    if (nresults == 0) {
        return 0.0;
    }
    
    if (!lua_isnumber(L, -1)) {
        luaL_error(L, "API function '%s' did not return a number", funcname);
        return 0.0;
    }
    
    return lua_tonumber(L, -1);
}
