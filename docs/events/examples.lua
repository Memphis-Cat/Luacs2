local events = require("cs2.events")

local death_id = events.on_post("player_death", function(event)
    local victim = event:get_player("userid")
    local attacker = event:get_player("attacker")
    local weapon = event:get_string("weapon", "")
    local headshot = event:get_bool("headshot", false)

    print(
        "death:",
        victim and victim.name or "unknown",
        "attacker:",
        attacker and attacker.name or "world",
        "weapon:",
        weapon,
        "headshot:",
        headshot
    )
end)

function OnUnload()
    events.off(death_id)
end
