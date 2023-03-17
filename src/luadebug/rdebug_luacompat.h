#pragma once

#include <lua.hpp>

#if LUA_VERSION_NUM == 501

#    undef lua_setglobal
#    undef lua_getglobal

#    define LUA_OK 0

inline int lua_absindex(lua_State* L, int idx) {
    if (idx > LUA_REGISTRYINDEX && idx < 0)
        return lua_gettop(L) + idx + 1;
    else
        return idx;
}

inline void lua_rawgetp(lua_State* L, int idx, const void* p) {
    idx = lua_absindex(L, idx);
    lua_pushlightuserdata(L, (void*)(p));
    lua_rawget(L, idx);
}

inline void lua_rawsetp(lua_State* L, int idx, const void* p) {
    idx = lua_absindex(L, idx);
    lua_pushlightuserdata(L, (void*)(p));
    lua_insert(L, -2);
    lua_rawset(L, idx);
}

inline void lua_setglobal(lua_State* L, const char* s) {
    lua_setfield(L, LUA_GLOBALSINDEX, (s));
}

inline void lua_getglobal(lua_State* L, const char* s) {
    lua_getfield(L, LUA_GLOBALSINDEX, (s));
}

inline const char* luaL_tolstring(lua_State* L, int idx, size_t* len) {
    if (!luaL_callmeta(L, idx, "__tostring")) {
        int t = lua_type(L, idx), tt = 0;
        char const* name = NULL;
        switch (t) {
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
        case LUA_TSTRING:
        case LUA_TNUMBER:
            lua_pushvalue(L, idx);
            break;
        case LUA_TBOOLEAN:
            if (lua_toboolean(L, idx))
                lua_pushliteral(L, "true");
            else
                lua_pushliteral(L, "false");
            break;
        default:
            tt   = luaL_getmetafield(L, idx, "__name");
            name = (tt == LUA_TSTRING) ? lua_tostring(L, -1) : lua_typename(L, t);
            lua_pushfstring(L, "%s: %p", name, lua_topointer(L, idx));
            if (tt != LUA_TNIL)
                lua_replace(L, -2);
            break;
        }
    }
    else {
        if (!lua_isstring(L, -1))
            luaL_error(L, "'__tostring' must return a string");
    }
    return lua_tolstring(L, -1, len);
}

inline size_t lua_rawlen(lua_State* L, int idx) {
    return lua_objlen(L, idx);
}

#endif

namespace lua {

#if LUA_VERSION_NUM >= 503
#    define LUACOMPAT_DEF(name)     \
        template <typename... Args> \
        int name(lua_State* L, Args... args) { return lua_##name(L, args...); }
#else
#    define LUACOMPAT_DEF(name)                \
        template <typename... Args>            \
        int name(lua_State* L, Args... args) { \
            lua_##name(L, args...);            \
            return lua_type(L, -1);            \
        }
#endif

    LUACOMPAT_DEF(rawgeti)
    LUACOMPAT_DEF(rawgetp)
    LUACOMPAT_DEF(getglobal)
    LUACOMPAT_DEF(getfield)
}

#if LUA_VERSION_NUM == 501
inline int lua_getiuservalue(lua_State* L, int idx, int n) {
    if (n != 1) {
        lua_pushnil(L);
        return LUA_TNONE;
    }
    lua_getfenv(L, idx);
    return lua_type(L, -1);
}
#elif LUA_VERSION_NUM == 502
inline int lua_getiuservalue(lua_State* L, int idx, int n) {
    if (n != 1) {
        lua_pushnil(L);
        return LUA_TNONE;
    }
    lua_getuservalue(L, idx);
    return lua_type(L, -1);
}
#elif LUA_VERSION_NUM == 503
inline int lua_getiuservalue(lua_State* L, int idx, int n) {
    if (n != 1) {
        lua_pushnil(L);
        return LUA_TNONE;
    }
    return lua_getuservalue(L, idx);
}
#endif

#if LUA_VERSION_NUM == 501
inline int lua_setiuservalue(lua_State* L, int idx, int n) {
    if (n != 1) {
        lua_pop(L, 1);
        return 0;
    }
    luaL_checktype(L, -1, LUA_TTABLE);
    lua_setfenv(L, idx);
    return 1;
}
#elif LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
inline int lua_setiuservalue(lua_State* L, int idx, int n) {
    if (n != 1) {
        lua_pop(L, 1);
        return 0;
    }
    lua_setuservalue(L, idx);
    return 1;
}
#endif

#ifdef LUAJIT_VERSION
#    include <lj_cdata.h>
#    include <lj_ctype.h>
#    include <lj_obj.h>
#    include <lj_tab.h>
using Table   = GCtab;
using Closure = GCfunc;
using UpVal   = GCupval;
using Proto   = GCproto;
using UDate   = GCudata;
using TString = GCstr;
using StkId   = TValue*;

inline TValue* index2adr(lua_State* L, int idx) {
    if (idx > 0) {
        TValue* o = L->base + (idx - 1);
        return o < L->top ? o : niltv(L);
    }
    else if (idx > LUA_REGISTRYINDEX) {
        lj_checkapi(idx != 0 && -idx <= L->top - L->base, "bad stack slot %d", idx);
        return L->top + idx;
    }
    else if (idx == LUA_GLOBALSINDEX) {
        TValue* o = &G(L)->tmptv;
        settabV(L, o, tabref(L->env));
        return o;
    }
    else if (idx == LUA_REGISTRYINDEX) {
        return registry(L);
    }
    else {
        GCfunc* fn = curr_func(L);
        lj_checkapi(fn->c.gct == ~LJ_TFUNC && !isluafunc(fn), "calling frame is not a C function");
        if (idx == LUA_ENVIRONINDEX) {
            TValue* o = &G(L)->tmptv;
            settabV(L, o, tabref(fn->c.env));
            return o;
        }
        else {
            idx = LUA_GLOBALSINDEX - idx;
            return idx <= fn->c.nupvalues ? &fn->c.upvalue[idx - 1] : niltv(L);
        }
    }
}
inline int lua_isinteger(lua_State* L, int idx) {
    cTValue* o = index2adr(L, idx);
    return tvisint(o);
}
#else
#    include <lapi.h>
#    include <lgc.h>
#    include <lobject.h>
#    include <lstate.h>
#    include <ltable.h>
#endif
