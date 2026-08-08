local timers = require("cs2.timers")

local started = timers.now()

local repeating
repeating = timers.every(1.0, function()
    print("uptime since example start:", timers.now() - started)
end)

timers.after(10.0, function()
    timers.cancel(repeating)
    print("repeating timer stopped")
end)
