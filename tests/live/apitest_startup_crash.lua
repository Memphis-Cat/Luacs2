plugin = {
    name = "LuaCS API Test Startup Crash",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Intentionally fails during startup to test load isolation."
}

print("[LuaTestStartupCrash] throwing intentional startup error now")
error("INTENTIONAL LuaCS startup isolation test")
