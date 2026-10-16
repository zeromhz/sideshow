#pragma once
#include <cstdint>
#include "projectM.h"
extern "C" {
typedef struct projectm_playlist* projectm_playlist_handle;
typedef void (*projectm_playlist_preset_switched_event)(bool, unsigned int, void*);
typedef void (*projectm_playlist_preset_switch_failed_event)(const char*, const char*, void*);
projectm_playlist_handle projectm_playlist_create(projectm_handle);
void projectm_playlist_destroy(projectm_playlist_handle);
void projectm_playlist_set_shuffle(projectm_playlist_handle, bool);
bool projectm_playlist_get_shuffle(projectm_playlist_handle);
void projectm_playlist_set_retry_count(projectm_playlist_handle, uint32_t);
void projectm_playlist_set_preset_switched_event_callback(projectm_playlist_handle, projectm_playlist_preset_switched_event, void*);
void projectm_playlist_set_preset_switch_failed_event_callback(projectm_playlist_handle, projectm_playlist_preset_switch_failed_event, void*);
void projectm_playlist_clear(projectm_playlist_handle);
uint32_t projectm_playlist_add_presets(projectm_playlist_handle, const char**, uint32_t, bool);
uint32_t projectm_playlist_size(projectm_playlist_handle);
char* projectm_playlist_item(projectm_playlist_handle, uint32_t);
void projectm_playlist_free_string(char*);
bool projectm_playlist_remove_preset(projectm_playlist_handle, uint32_t);
uint32_t projectm_playlist_set_position(projectm_playlist_handle, uint32_t, bool);
uint32_t projectm_playlist_play_next(projectm_playlist_handle, bool);
uint32_t projectm_playlist_play_previous(projectm_playlist_handle, bool);
}
