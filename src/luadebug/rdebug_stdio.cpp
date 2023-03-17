#include <limits>
#include <new>

#include "rdebug_lua.h"
#include "rdebug_redirect.h"

lua_State* get_host(luadbg_State* L);
luadbg_State* get_client(lua_State* L);
int event(luadbg_State* cL, lua_State* hL, const char* name, int start);

static int getIoOutput(lua_State* L) {
#if LUA_VERSION_NUM >= 502
    return lua::getfield(L, LUA_REGISTRYINDEX, "_IO_output");
#else
    return lua::rawgeti(L, LUA_ENVIRONINDEX, 2);
#endif
}

static int redirect_read(luadbg_State* L) {
    luadebug::redirect& self = *(luadebug::redirect*)luadbgL_checkudata(L, 1, "redirect");
    luadbg_Integer len       = luadbgL_optinteger(L, 2, LUAL_BUFFERSIZE);
    if (len > (std::numeric_limits<int>::max)()) {
        return luadbgL_error(L, "bad argument #1 to 'read' (invalid number)");
    }
    if (len <= 0) {
        return 0;
    }
    luadbgL_Buffer b;
    luadbgL_buffinit(L, &b);
    char* buf = luadbgL_prepbuffsize(&b, (size_t)len);
    size_t rc = self.read(buf, (size_t)len);
    if (rc == 0) {
        return 0;
    }
    luadbgL_pushresultsize(&b, rc);
    return 1;
}

static int redirect_peek(luadbg_State* L) {
#if defined(_WIN32)
    luadebug::redirect& self = *(luadebug::redirect*)luadbgL_checkudata(L, 1, "redirect");
    luadbg_pushinteger(L, self.peek());
#else
    luadbg_pushinteger(L, LUAL_BUFFERSIZE);
#endif
    return 1;
}

static int redirect_close(luadbg_State* L) {
    luadebug::redirect& self = *(luadebug::redirect*)luadbgL_checkudata(L, 1, "redirect");
    self.close();
    return 0;
}

static int redirect_gc(luadbg_State* L) {
    luadebug::redirect& self = *(luadebug::redirect*)luadbgL_checkudata(L, 1, "redirect");
    self.close();
    self.~redirect();
    return 0;
}

static int redirect(luadbg_State* L) {
    const char* lst[]     = { "stdin", "stdout", "stderr", NULL };
    luadebug::std_fd type = (luadebug::std_fd)(luadbgL_checkoption(L, 1, "stdout", lst));
    switch (type) {
    case luadebug::std_fd::STDIN:
    case luadebug::std_fd::STDOUT:
    case luadebug::std_fd::STDERR:
        break;
    default:
        return 0;
    }
    luadebug::redirect* r = (luadebug::redirect*)luadbg_newuserdata(L, sizeof(luadebug::redirect));
    new (r) luadebug::redirect;
    if (!r->open(type)) {
        return 0;
    }
    if (luadbgL_newmetatable(L, "redirect")) {
        static luadbgL_Reg mt[] = {
            { "read", redirect_read },
            { "peek", redirect_peek },
            { "close", redirect_close },
            { "__gc", redirect_gc },
            { NULL, NULL }
        };
        luadbgL_setfuncs(L, mt, 0);
        luadbg_pushvalue(L, -1);
        luadbg_setfield(L, -2, "__index");
    }
    luadbg_setmetatable(L, -2);
    return 1;
}

static int callfunc(lua_State* L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static int redirect_print(lua_State* L) {
    luadbg_State* cL = get_client(L);
    if (cL) {
        int ok = event(cL, L, "print", 1);
        if (ok > 0) {
            return 0;
        }
    }
    return callfunc(L);
}

static int redirect_f_write(lua_State* L) {
    bool ok = LUA_TUSERDATA == getIoOutput(L) && lua_rawequal(L, -1, 1);
    lua_pop(L, 1);
    if (ok) {
        luadbg_State* cL = get_client(L);
        if (cL) {
            int ok = event(cL, L, "iowrite", 2);
            if (ok > 0) {
                lua_settop(L, 1);
                return 1;
            }
        }
    }
    return callfunc(L);
}

static int redirect_io_write(lua_State* L) {
    luadbg_State* cL = get_client(L);
    if (cL) {
        int ok = event(cL, L, "iowrite", 1);
        if (ok > 0) {
            getIoOutput(L);
            return 1;
        }
    }
    return callfunc(L);
}

static bool openhook(lua_State* L, bool enable, lua_CFunction f) {
    if (enable) {
        lua_pushcclosure(L, f, 1);
        return true;
    }
    if (lua_tocfunction(L, -1) == f) {
        if (lua_getupvalue(L, -1, 1)) {
            lua_remove(L, -2);
            return true;
        }
    }
    lua_pop(L, 1);
    return false;
}

static int open_print(luadbg_State* L) {
    bool enable   = luadbg_toboolean(L, 1);
    lua_State* hL = get_host(L);
    lua_getglobal(hL, "print");
    if (openhook(hL, enable, redirect_print)) {
        lua_setglobal(hL, "print");
    }
    return 0;
}

static int open_iowrite(luadbg_State* L) {
    bool enable   = luadbg_toboolean(L, 1);
    lua_State* hL = get_host(L);
    if (LUA_TUSERDATA == getIoOutput(hL)) {
        if (lua_getmetatable(hL, -1)) {
#if LUA_VERSION_NUM >= 504
            lua_pushstring(hL, "__index");
            if (LUA_TTABLE == lua_rawget(hL, -2)) {
                lua_remove(hL, -2);
#endif
                lua_pushstring(hL, "write");
                lua_pushvalue(hL, -1);
                lua_rawget(hL, -3);
                if (openhook(hL, enable, redirect_f_write)) {
                    lua_rawset(hL, -3);
                }
                lua_pop(hL, 1);
#if LUA_VERSION_NUM >= 504
            }
            else {
                lua_pop(hL, 1);
            }
#endif
        }
    }
    lua_pop(hL, 1);
    if (LUA_TTABLE == lua::getglobal(hL, "io")) {
        lua_pushstring(hL, "write");
        lua_pushvalue(hL, -1);
        lua_rawget(hL, -3);
        if (openhook(hL, enable, redirect_io_write)) {
            lua_rawset(hL, -3);
        }
    }
    lua_pop(hL, 1);
    return 0;
}

LUADEBUG_FUNC
int luaopen_luadebug_stdio(luadbg_State* L) {
    luadbg_newtable(L);
    static luadbgL_Reg lib[] = {
        { "redirect", redirect },
        { "open_print", open_print },
        { "open_iowrite", open_iowrite },
        { NULL, NULL },
    };
    luadbgL_setfuncs(L, lib, 0);
    return 1;
}
