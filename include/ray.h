#pragma once

// The pinned AlliedModders CS2 HL2SDK revision defines Ray_t and RayType_t in
// cmodel.h. Newer revisions moved the same API to ray.h. Keeping this tiny
// compatibility header lets the implementation use the modern include name
// without changing or shadowing any type definitions.
#include <cmodel.h>
