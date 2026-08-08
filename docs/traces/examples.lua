local traces = require("cs2.traces")
local commands = require("cs2.commands")

commands.on("trace_forward", function(player)
    if not player then return end

    local refreshed, err = player:refresh()
    if not refreshed then
        print("player refresh failed:", err)
        return
    end

    local result, trace_err = traces.direction(
        player.position,
        Vector(1, 0, 0),
        1024,
        {
            mask = traces.MASK_SHOT_FULL,
            ignore = player,
        }
    )

    if not result then
        print("trace failed:", trace_err)
        return
    end

    print("hit:", result.hit)
    print("fraction:", result.fraction)
    print("position:", result.position)
    print("entity:", result.entity_index)
end)
