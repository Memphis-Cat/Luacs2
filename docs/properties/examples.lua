local properties = require("cs2.properties")
local commands = require("cs2.commands")

commands.on("health_property", function(player)
    if not player then return end

    local info, err = properties.info(player, "m_iHealth")
    if not info then
        print("info failed:", err)
        return
    end

    local value, read_err = properties.get_int(player, "m_iHealth")
    if value == nil then
        print("read failed:", read_err)
        return
    end

    print("type:", info.type_name)
    print("kind:", info.kind_name)
    print("health:", value)
end)
