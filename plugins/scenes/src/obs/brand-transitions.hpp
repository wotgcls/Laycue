// Laycue's branded scene transitions: seven broadcast moves (Push, Brand Wipe, Zoom Through, Split Doors, Logo
// Sting, Card Stack, Stripes) that appear in OBS's own transition list. Each takes two brand colours and an optional
// logo picture or animated GIF, set in the transition's Properties. Talks to libobs only.
#pragma once

namespace laycue {

// Call once from obs_module_load.
void registerBrandTransitions();

} // namespace laycue
