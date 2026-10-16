#include "isf_playback.h"
#include "isf_document.h"
#include <algorithm>
#include <cmath>

namespace sideshow {
IsfPlayback::IsfPlayback(VisualSelection selection, PlaybackSettings settings, uint32_t seed)
    : settings_(settings), selected_(std::move(selection)),
      requested_(selected_.preset), requestedFolder_(selected_.playlist), random_(seed) {}

void IsfPlayback::setCatalog(std::shared_ptr<const IsfCatalog> catalog) {
    catalog_ = std::move(catalog);
    if (loading_) resolve();
}

void IsfPlayback::request() { outgoing_ = IsfPlaybackRequest{generation_, requested_}; }

bool IsfPlayback::resolve() {
    if (!catalog_) return true;
    candidates_.clear();
    candidateIndex_ = 0;
    if (folderRequest_) {
        candidates_ = catalog_->inFolder(requestedFolder_);
    } else if (!requested_.empty() && catalog_->find(requested_)) {
        candidates_.push_back(requested_);
        if (startup_) {
            for (const auto& id : catalog_->inFolder(catalog_->find(requested_)->folder))
                if (id != requested_) candidates_.push_back(id);
        }
    } else if (startup_) {
        for (const auto& entry : catalog_->entries()) candidates_.push_back(entry.id);
    }
    if (candidates_.empty()) {
        loading_ = false;
        error_ = "Unknown or empty ISF selection";
        outgoing_ = IsfPlaybackRequest{generation_, ""}; // cancel an older worker request
        return false;
    }
    requested_ = candidates_.front();
    request();
    return true;
}

bool IsfPlayback::begin(std::string id, bool folder) {
    ++generation_;
    controlsAllowed_ = false;
    controls_.clear();
    candidates_.clear();
    candidateIndex_ = 0;
    startup_ = false;
    autoStopped_ = false;
    loading_ = true;
    error_.clear();
    folderRequest_ = folder;
    if (folder) requestedFolder_ = std::move(id);
    else requested_ = std::move(id);
    return resolve();
}

bool IsfPlayback::selectPreset(const std::string& id) {
    bool valid = validIsfId(id);
    bool accepted = begin(valid ? id : "", false);
    return valid && accepted;
}

bool IsfPlayback::selectPlaylist(const std::string& folder) {
    bool valid = folder == "." || validIsfId(folder);
    bool accepted = begin(valid ? folder : "", true);
    return valid && accepted;
}

void IsfPlayback::navigate(int direction, bool shuffled) {
    if (!catalog_) return;
    std::string base = loading_ ? requested_ : selected_.preset;
    std::string folder = selected_.playlist;
    if (auto entry = catalog_->find(base)) folder = entry->folder;
    auto ids = catalog_->inFolder(folder);
    if (ids.empty()) { begin("", false); return; }
    auto found = std::find(ids.begin(), ids.end(), base);
    size_t index = found == ids.end() ? 0 : size_t(found - ids.begin());
    if (shuffled && ids.size() > 1) {
        std::uniform_int_distribution<size_t> pick(1, ids.size() - 1);
        index = (index + pick(random_)) % ids.size();
    } else {
        index = (index + ids.size() + direction) % ids.size();
    }
    begin(ids[index], false);
    // Navigation may try each direct child once; an explicit preset may not.
    candidates_.clear();
    for (size_t n = 0; n < ids.size(); ++n)
        candidates_.push_back(ids[(index + n) % ids.size()]);
    candidateIndex_ = 0;
}

void IsfPlayback::presented(double seconds) {
    if (std::isfinite(seconds)) elapsed_ += std::max(0.0, seconds);
    if (!loading_ && !autoStopped_ && !settings_.locked && elapsed_ >= settings_.duration) {
        elapsed_ = 0;
        navigate(1, settings_.shuffle);
    }
}

std::optional<IsfPlaybackRequest> IsfPlayback::takeRequest() {
    auto result = std::move(outgoing_);
    outgoing_.reset();
    return result;
}

bool IsfPlayback::accepts(uint64_t generation) const {
    return loading_ && generation == generation_;
}

void IsfPlayback::failed(uint64_t generation, std::string error) {
    if (!accepts(generation)) return;
    error_ = std::move(error);
    if (candidateIndex_ + 1 < candidates_.size()) {
        requested_ = candidates_[++candidateIndex_];
        ++generation_;
        request();
        return;
    }
    loading_ = false;
    autoStopped_ = true;
    controls_.clear();
}

bool IsfPlayback::commit(uint64_t generation, const std::string& id) {
    if (!accepts(generation) || id != requested_ || !catalog_) return false;
    auto entry = catalog_->find(id);
    if (!entry) return false;
    selected_ = {entry->folder, id};
    loading_ = false;
    startup_ = false;
    controlsAllowed_ = true;
    elapsed_ = 0;
    controls_.clear();
    error_.clear();
    return true;
}

IsfControlRoute IsfPlayback::route(const IsfControlCommand& command) {
    if (loading_) {
        if (controls_.size() == 64) return IsfControlRoute::Full;
        controls_.push_back(command);
        return IsfControlRoute::Queued;
    }
    return controlsAllowed_ ? IsfControlRoute::Apply : IsfControlRoute::Rejected;
}
}
