#include "milkdrop_backend.h"
namespace sideshow {
MilkdropBackend::MilkdropBackend(const BackendInit& i)
    :renderer_(i.width,i.height,i.config.display.fps,i.config.presets),
     catalog_(i.config.presets.root),presets_(renderer_.handle(),catalog_,i.playback.shuffle) {
    presets_.start(i.selection.playlist);
    if(!i.selection.preset.empty()) presets_.loadPreset(i.selection.preset);
    setSettings(i.playback);
}
void MilkdropBackend::setSettings(const PlaybackSettings& s) {
    presets_.setLocked(s.locked); renderer_.setPresetDuration(s.duration);
    renderer_.setBeatSensitivity(s.beatSensitivity);renderer_.setHardCutEnabled(s.hardcut);
}
}
