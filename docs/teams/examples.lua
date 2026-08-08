local teams = require("cs2.teams")
local commands = require("cs2.commands")

commands.on("teams", function()
    print("T score:", teams.get_score(teams.T))
    print("CT score:", teams.get_score(teams.CT))

    for _, player in ipairs(teams.get_players(teams.CT)) do
        print("CT:", player.name)
    end
end)
