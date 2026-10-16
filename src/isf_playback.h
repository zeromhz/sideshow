#pragma once
#include "isf_catalog.h"
#include "isf_parameters.h"
#include "visual_backend.h"
#include <optional>
#include <random>

namespace sideshow {
struct IsfPlaybackRequest { uint64_t generation; std::string id; };
enum class IsfControlRoute { Queued, Apply, Rejected, Full };

// Selection and command ownership are independent of loading and GL rendering.
// The adapter drains requests and commits only after a successful first frame.
class IsfPlayback {
public:
    IsfPlayback(VisualSelection, PlaybackSettings, uint32_t seed);
    void setCatalog(std::shared_ptr<const IsfCatalog>);
    bool selectPreset(const std::string&);
    bool selectPlaylist(const std::string&);
    void navigate(int direction, bool shuffled);
    void setSettings(const PlaybackSettings& settings) { settings_ = settings; }
    void presented(double seconds);
    std::optional<IsfPlaybackRequest> takeRequest();
    bool accepts(uint64_t generation) const;
    void failed(uint64_t generation, std::string error);
    bool commit(uint64_t generation, const std::string& id);
    IsfControlRoute route(const IsfControlCommand&);
    const std::vector<IsfControlCommand>& controls() const { return controls_; }
    const VisualSelection& selection() const { return selected_; }
    bool pending() const { return loading_; }
    const std::string& error() const { return error_; }
private:
    bool begin(std::string id, bool folder);
    bool resolve();
    void request();
    PlaybackSettings settings_;
    VisualSelection selected_;
    std::shared_ptr<const IsfCatalog> catalog_;
    uint64_t generation_ = 1;
    std::string requested_, requestedFolder_, error_;
    bool loading_ = true, startup_ = true, folderRequest_ = false;
    bool controlsAllowed_ = true, autoStopped_ = false;
    double elapsed_ = 0;
    std::vector<std::string> candidates_;
    size_t candidateIndex_ = 0;
    std::vector<IsfControlCommand> controls_;
    std::optional<IsfPlaybackRequest> outgoing_;
    std::mt19937 random_;
};
}
