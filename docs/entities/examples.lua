local entities = require("cs2.entities")
local commands = require("cs2.commands")

commands.on("entity_count", function()
    local count, err = entities.count_by_classname("*")
    if not count then
        print("entity count failed:", err)
        return
    end
    print("live entities:", count)
end)

commands.on("find_c4", function()
    for _, entity in ipairs(entities.find_by_classname("weapon_c4")) do
        print("c4 entity:", entity.entity_index, entity.position)
    end
end)
