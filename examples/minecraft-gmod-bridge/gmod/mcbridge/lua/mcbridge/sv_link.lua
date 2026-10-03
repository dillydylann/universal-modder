-- Server: the link to Minecraft. Every tick (one request in flight at a time) the server POSTs its state and
-- everything queued since the last poll to the gmodbridge mod's HTTP server on 127.0.0.1, and hands the reply's
-- events and mob list to the world and mob code.
--
-- Garry's Mod only lets Lua reach localhost when the game is started with -allowlocalhttp.

MCB.cvEnabled = CreateConVar("mcbridge_enabled", "1", FCVAR_ARCHIVE, "Talk to Minecraft")
MCB.cvPort = CreateConVar("mcbridge_port", "25600", FCVAR_ARCHIVE, "Port of the gmodbridge mod's HTTP server on 127.0.0.1")

MCB.Connected = false
MCB.Handlers = MCB.Handlers or {}

local out = {}
local inflight = false
local nextTry = 0
local warned = false

--- Append an item to a list in the next poll ("dmg", "explode", "put", ...).
function MCB.Queue(key, item)
	local list = out[key]
	if not list then
		list = {}
		out[key] = list
	end

	list[#list + 1] = item
end

--- Append several numbers to a flat list in the next poll ("solid", "water", "take", ...).
function MCB.QueueFlat(key, ...)
	local list = out[key]
	if not list then
		list = {}
		out[key] = list
	end

	for _, v in ipairs({...}) do
		list[#list + 1] = v
	end
end

--- Set a single value in the next poll ("clear", "sync", ...).
function MCB.Set(key, value)
	out[key] = value
end

--- The player whose world is mirrored (the listen server's host, i.e. you in single-player).
function MCB.Host()
	for _, ply in ipairs(player.GetHumans()) do
		if ply:IsListenServerHost() or game.SinglePlayer() then return ply end
	end

	return player.GetHumans()[1]
end

local function handle(reply)
	if not istable(reply) then return end

	-- Minecraft's server is up before its world is (and between worlds): everything sent meanwhile is dropped, so
	-- only count as connected once the world is ready, and start over (rescan, resync) each time it becomes ready
	if not reply.ready then
		if MCB.Connected then
			print("[mcbridge] Minecraft's world closed; waiting for it")
			MCB.Connected = false
			hook.Run("MCBridgeDisconnected")
		end

		return
	end

	if not MCB.Connected then
		MCB.Connected = true
		warned = false
		print("[mcbridge] connected to Minecraft")
		hook.Run("MCBridgeConnected")
	end

	for _, e in ipairs(reply.events or {}) do
		local f = istable(e) and MCB.Handlers[e.t]
		if f then
			local ok, err = pcall(f, e)
			if not ok then ErrorNoHalt("[mcbridge] " .. tostring(e.t) .. ": " .. tostring(err) .. "\n") end
		end
	end

	if MCB.OnMobs then MCB.OnMobs(reply.mobs or {}) end
end

local function disconnected(why)
	if MCB.Connected then
		print("[mcbridge] lost Minecraft: " .. tostring(why))
		MCB.Connected = false
		hook.Run("MCBridgeDisconnected")
	elseif not warned then
		warned = true
		print("[mcbridge] can't reach Minecraft on 127.0.0.1:" .. MCB.cvPort:GetInt() .. " (" .. tostring(why) .. "). Is it running with the gmodbridge mod, and was GMod started with -allowlocalhttp?")
	end

	nextTry = CurTime() + 2
end

local started = false
hook.Add("InitPostEntity", "mcb_link", function() started = true end)

hook.Add("Tick", "mcb_link", function()
	if not started or inflight or not MCB.cvEnabled:GetBool() or CurTime() < nextTry then return end

	local host = MCB.Host()
	if not IsValid(host) then return end

	-- everything else adds its part of the state (player, people, held mobs, map cells) here
	hook.Run("MCBridgePoll", host)

	local body = out
	body.v = 1 -- never an empty table: TableToJSON would turn that into [] and Minecraft wants an object
	out = {}
	inflight = true
	local ok = HTTP({
		url = "http://127.0.0.1:" .. MCB.cvPort:GetInt() .. "/tick",
		method = "POST",
		type = "application/json",
		body = util.TableToJSON(body),
		headers = { ["X-MCBridge"] = "1" },
		timeout = 5,
		success = function(code, text)
			inflight = false
			if code ~= 200 then
				disconnected("HTTP " .. tostring(code))
				return
			end

			handle(util.JSONToTable(text))
		end,
		failed = function(reason)
			inflight = false
			disconnected(reason)
		end,
	})

	if not ok then
		inflight = false
		disconnected("HTTP() refused the request")
	end
end)
