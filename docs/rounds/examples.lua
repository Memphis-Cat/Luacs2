local rounds = require("cs2.rounds")
local commands = require("cs2.commands")

commands.on("round_state", function()
    local state, err = rounds.state()
    if not state then
        print("round state failed:", err)
        return
    end

    print("round:", state.number)
    print("frozen:", state.frozen)
    print("win reason:", state.win_reason)
end)
