#pragma once
#include <cstdint>

class App;

// Signal that the builder should open (create mode if edit_id==0, else edit mode).
// Safe to call from any UI panel (no ImGui popup context required).
void formula_builder_request_open(uint32_t edit_id = 0);

// Call every frame from App::render() to handle deferred opens and render the modal.
void formula_builder_render(App& app);
