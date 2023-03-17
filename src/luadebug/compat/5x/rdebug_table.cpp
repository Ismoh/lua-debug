#include "rdebug_table.h"

#include "rdebug_luacompat.h"

namespace luadebug::table {

#if LUA_VERSION_NUM < 504
#    define s2v(o) (o)
#endif

    template <typename T>
    StkId& LUA_STKID(T& s) {
#if defined(LUA_VERSION_LATEST)
        return s.p;
#else
        return s;
#endif
    }

    static unsigned int array_limit(const Table* t) {
#if LUA_VERSION_NUM >= 504
        if ((!(t->marked & BITRAS) || (t->alimit & (t->alimit - 1)) == 0)) {
            return t->alimit;
        }
        unsigned int size = t->alimit;
        size |= (size >> 1);
        size |= (size >> 2);
        size |= (size >> 4);
        size |= (size >> 8);
        size |= (size >> 16);
#    if (UINT_MAX >> 30) > 3
        size |= (size >> 32);
#    endif
        size++;
        return size;
#else
        return t->sizearray;
#endif
    }

    unsigned int array_size(const void* tv) {
        const Table* t      = (const Table*)tv;
        unsigned int alimit = array_limit(t);
        if (alimit) {
            for (unsigned int i = alimit; i > 0; --i) {
                if (!ttisnil(&t->array[i - 1])) {
                    return i;
                }
            }
        }
        return 0;
    }

    unsigned int hash_size(const void* tv) {
        const Table* t = (const Table*)tv;
        return (unsigned int)(1 << t->lsizenode);
    }

    bool has_zero(const void* tv) {
        return false;
    }

    int get_zero(lua_State* L, const void* tv) {
        return 0;
    }

    int get_kv(lua_State* L, const void* tv, unsigned int i) {
        const Table* t = (const Table*)tv;

        Node* n = &t->node[i];
        if (ttisnil(gval(n))) {
            return 0;
        }

        LUA_STKID(L->top) += 2;
        StkId key = LUA_STKID(L->top) - 1;
        StkId val = LUA_STKID(L->top) - 2;
#if LUA_VERSION_NUM >= 504
        getnodekey(L, s2v(key), n);
#else
        setobj2s(L, key, &n->i_key.tvk);
#endif
        setobj2s(L, val, gval(n));
        return 1;
    }

    int get_k(lua_State* L, const void* t, unsigned int i) {
        if (i >= hash_size(t)) {
            return 0;
        }
        Node* n = &((const Table*)t)->node[i];
        if (ttisnil(gval(n))) {
            return 0;
        }
        StkId key = LUA_STKID(L->top);
#if LUA_VERSION_NUM >= 504
        getnodekey(L, s2v(key), n);
#else
        setobj2s(L, key, &n->i_key.tvk);
#endif
        LUA_STKID(L->top) += 1;
        return 1;
    }

    int get_k(lua_State* L, int idx, unsigned int i) {
        const void* t = lua_topointer(L, idx);
        if (!t) {
            return 0;
        }
        return get_k(L, t, i);
    }

    int get_v(lua_State* L, int idx, unsigned int i) {
        const Table* t = (const Table*)lua_topointer(L, idx);
        if (!t) {
            return 0;
        }
        if (i >= hash_size(t)) {
            return 0;
        }

        Node* n = &t->node[i];
        if (ttisnil(gval(n))) {
            return 0;
        }
        setobj2s(L, LUA_STKID(L->top), gval(n));

        LUA_STKID(L->top) += 1;
        return 1;
    }

    int set_v(lua_State* L, int idx, unsigned int i) {
        const Table* t = (const Table*)lua_topointer(L, idx);

        if (!t) {
            return 0;
        }
        if (i >= hash_size(t)) {
            return 0;
        }

        Node* n = &t->node[i];
        if (ttisnil(gval(n))) {
            return 0;
        }
        setobj2t(L, gval(n), s2v(LUA_STKID(L->top) - 1));

        LUA_STKID(L->top) -= 1;
        return 1;
    }

}
