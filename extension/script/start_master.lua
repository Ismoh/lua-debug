return function (debug, runtime, addr)
    local thread = require "remotedebug.thread"
    local ok, err = pcall(thread.newchannel, "DbgMaster")
    if not ok then
        if err:sub(1,17) == "Duplicate channel" then
            return
        end
        error(err)
    end
    thread.thread (([[
        local debug = %q
        local runtime = %q
        package.path = debug..'/script/?.lua'
        package.cpath = debug..runtime..'/?.dll'
        local thread = require "remotedebug.thread"
        local err = thread.channel "errlog"
        local log = require "common.log"
        log.file = debug.."/error.log"
        while true do
            log.error("ERROR:" .. err:bpop())
        end
    ]]):format(debug, runtime))

    local bootstrap = ([=[
        local debug = %q
        local runtime = %q
        local addr = %q
        package.path = debug..'/script/?.lua'
        package.cpath = debug..runtime..'/?.dll'

        local function split(str)
            local r = {}
            str:gsub('[^:]+', function (w) r[#r+1] = w end)
            return r
        end
        local l = split(addr)
        local t = {}
        if #l <= 1 then
            t.protocol = 'tcp'
            t.address = addr
            t.port = 4278
        else
            if l[1] == 'pipe' then
                t.protocol = 'unix'
                t.address = addr:sub(6)
                t.client = true
            elseif l[1] == 'listen' then
                t.protocol = 'tcp'
                if #l == 2 then
                    t.address = l[2]
                    t.port = 4278
                else
                    t.address = l[2]
                    t.port = tonumber(l[3])
                end
            else
                t.protocol = 'tcp'
                t.address = l[1]
                t.port = tonumber(l[2])
            end
        end
        if t.address == "localhost" then
            t.address = "127.0.0.1"
        end

        local serverFactory = require "common.serverFactory"
        local server = serverFactory(t)

        local dbg_io = {}
        function dbg_io:event_in(f)
            self.fsend = f
        end
        function dbg_io:event_close(f)
            self.fclose = f
        end
        function dbg_io:update()
            local data = server.recvRaw()
            if data ~= '' then
                self.fsend(data)
            end
            return true
        end
        function dbg_io:send(data)
            server.sendRaw(data)
        end
        function dbg_io:close()
            self.fclose()
        end

        local select = require "common.select"
        local master = require 'backend.master'
        master.init(dbg_io)
        while true do
            select.update(0.05)
            master.update()
        end
    ]=]):format(debug, runtime, addr)
    thread.thread(bootstrap)
end