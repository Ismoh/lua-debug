local rdebug = require 'remotedebug.visitor'
local hookmgr = require 'remotedebug.hookmgr'
local source = require 'backend.worker.source'
local luaver = require 'backend.worker.luaver'
local fs = require 'backend.worker.filesystem'

local info = {}

local function shortsrc(source, maxlen)
    maxlen = maxlen or 60
    local type = source:sub(1,1)
    if type == '=' then
        if #source <= maxlen then
            return source:sub(2)
        else
            return source:sub(2, maxlen)
        end
    elseif type == '@' then
        if #source <= maxlen then
            return source:sub(2)
        else
            return '...' .. source:sub(#source - maxlen + 5)
        end
    else
        local nl = source:find '\n'
        maxlen = maxlen - 15
        if #source < maxlen and nl == nil then
            return ('[string "%s"]'):format(source)
        else
            local n = #source
            if nl ~= nil then
                n = nl - 1
            end
            if n > maxlen then
                n = maxlen
            end
            return ('[string "%s..."]'):format(source:sub(1, n))
        end
    end
end

local function getshortsrc(src)
    if src.sourceReference then
        local code = source.getCode(src.sourceReference)
        return shortsrc(code)
    elseif src.path then
        return shortsrc('@' .. source.clientPath(src.path))
    elseif src.skippath then
        return shortsrc('@' .. source.clientPath(src.skippath))
    elseif info.source:sub(1,1) == '=' then
        return shortsrc(info.source)
    else
        -- TODO
        return '<unknown>'
    end
end

local function findfield(t, f, level, name)
    local loct = rdebug.tablehashv(t, 5000)
    for i = 1, #loct, 2 do
        local key, value = loct[i], loct[i+1]
        if rdebug.type(key) == 'string' then
            local skey = rdebug.value(key)
            if level ~= 0 or skey ~= '_G' then
                local tvalue = rdebug.type(value)
                if (tvalue == 'function' or tvalue == 'c function') and rdebug.value(value) == f then
                    return name and (name .. '.' .. skey) or skey
                end
                if level < 2 and tvalue == 'table' then
                    return findfield(value, f, level + 1, name and (name .. '.' .. skey) or skey)
                end
            end
        end
    end
end

local function pushglobalfuncname(f)
    f = rdebug.value(f)
    if f ~= nil then
        return findfield(rdebug._G, f, 2)
    end
end

local function pushfuncname(f, info)
    local funcname = pushglobalfuncname(f)
    if funcname then
        return ("function '%s'"):format(funcname)
    elseif info.namewhat ~= '' then
        return ("%s '%s'"):format(info.namewhat, info.name)
    elseif info.what == 'main' then
        return 'main chunk'
    elseif info.what ~= 'C' then
        local src = source.create(info.source)
        return ('function <%s:%d>'):format(getshortsrc(src), source.line(src, info.linedefined))
    else
        return '?'
    end
end

local function replacewhere(error)
    local msg = tostring(rdebug.value(error))
    local f, l = msg:find ':[-%d]+: '
    if not f then
        if rdebug.getinfo(1, "Sl", info) then
            local src = source.create(info.source)
            msg = ('%s:%d: %s'):format(getshortsrc(src), source.line(src, info.currentline), msg)
        end
        return msg, 1
    end
    local srcpath = fs.source_normalize(msg:sub(1, f-1))
    local line = tonumber(msg:sub(f+1, l-2))
    local message = msg:sub(l + 1)
    local level = 0
    while true do
        if not rdebug.getinfo(level, "Sl", info) then
            return ('%s:%d: %s'):format(source.clientPath(srcpath), line, message), 0
        end
        if info.what ~= 'C' then
            local src = source.create(info.source)
            return ('%s:%d: %s'):format(getshortsrc(src), source.line(src, info.currentline), message), level
        end
        level = level + 1
    end
end

return function(error)
    local s = {}
    local message, level = replacewhere(error)
    s[#s + 1] = 'stack traceback:'
    local last = hookmgr.stacklevel()
    local n1 = ((last - level) > 21) and 10 or -1
    local opt = luaver.LUAVERSION >= 52 and "Slnt" or "Sln"
    local depth = level
    while rdebug.getinfo(depth, opt, info) do
        local f = rdebug.getfunc(depth)
        depth = depth + 1
        n1 = n1 - 1
        if n1 == 1 then
            s[#s + 1] = '\n\t...'
            depth = last - 10
        else
            local src = source.create(info.source)
            s[#s + 1] = ('\n\t%s:'):format(getshortsrc(src))
            if info.currentline > 0 then
                s[#s + 1] = ('%d:'):format(source.line(src, info.currentline))
            end
            s[#s + 1] = " in "
            s[#s + 1] = pushfuncname(f, info)
            if info.istailcall then
                s[#s + 1] = '\n\t(...tail calls...)'
            end
        end
    end
    return message, table.concat(s), level
end
