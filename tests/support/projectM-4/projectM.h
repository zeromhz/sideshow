#pragma once
// Test-only subset of the projectM 4 C API used by PresetManager.
extern "C" {
typedef struct projectm* projectm_handle;
typedef void (*projectm_preset_switch_requested_event)(bool, void*);
void projectm_set_preset_switch_requested_event_callback(projectm_handle, projectm_preset_switch_requested_event, void*);
void projectm_load_preset_file(projectm_handle, const char*, bool);
void projectm_set_preset_locked(projectm_handle, bool);
}
