compile.exe writes authenticated .smg plugins to this folder.
Do not delete config\luacs.key after compiling plugins; the runtime needs the same key to load them.

example_welcome.lua is installed under scripting as source documentation only.
LuaCS does not auto-load a compiled example_welcome.smg during deployment.
Compile the example manually only on a test server where its gameplay changes are wanted.
