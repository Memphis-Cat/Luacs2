// Compile the complete grenade Lua module with its C++ stdio dependency made
// explicit in the translation unit. Do not rely on Lua headers to transitively
// provide std::snprintf under MSVC /WX.
#include <cstdio>

#include "grenades.cpp"
