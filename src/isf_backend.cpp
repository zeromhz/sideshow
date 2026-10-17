#include "isf_backend.h"
#include "isf_playback.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace sideshow {
struct IsfBackend::Impl {
    IsfLoader& loader;
    IsfContext& context;
    IsfBudget& budget;
    Config::Isf config;
    int width, height;
    uint64_t session, frame = 0;
    IsfPlayback playback;
    std::unique_ptr<IsfRuntime> runtime;
    std::unique_ptr<IsfParameters> parameters, lastGoodParameters;
    IsfAudio audio;
    bool prepared = false, dirty = false;
    double time = 0;

    Impl(const BackendInit& init, IsfLoader& l, IsfContext& c, IsfBudget& b)
        : loader(l), context(c), budget(b), config(init.config.isf),
          width(init.width), height(init.height), session(loader.acquireSession()),
          playback(init.selection, init.playback, std::random_device{}()) {}
    ~Impl() { loader.cancel(session); }

    std::pair<int, int> dimensions() const {
        int maxWidth = config.maxWidth, maxHeight = config.maxHeight;
        if (height > width) std::swap(maxWidth, maxHeight);
        double scale = std::min({1.0, double(maxWidth) / std::max(1, width),
                               double(maxHeight) / std::max(1, height)});
        return {std::max(1, int(width * scale)), std::max(1, int(height * scale))};
    }

    void dispatch() {
        if (auto request = playback.takeRequest())
            loader.request(session, request->generation, std::move(request->id));
    }

    void fail(uint64_t generation, const std::string& why) {
        logWarn("ISF: " + why);
        playback.failed(generation, why);
        dispatch();
    }

    CommandResult control(const IsfControlCommand& command) {
        auto route = playback.route(command);
        if (route == IsfControlRoute::Full) return {"rejected", "ISF pending control limit reached"};
        if (route == IsfControlRoute::Queued)
            return {"accepted", "ISF control queued; validation awaits preset loading"};
        if (route != IsfControlRoute::Apply || !runtime || !parameters)
            return {"rejected", "ISF control has no valid target"};
        std::string why;
        auto proposed = *parameters;
        if (!proposed.apply(command, why)) return {"rejected", why};
        auto size = dimensions();
        if (!runtime->update(proposed, size.first, size.second, why, prepared)) {
            return {"rejected", why};
        }
        *parameters = std::move(proposed);
        dirty = true;
        // Rendering can still reject this update and restore the last good values.
        return {"accepted", "ISF control validated; awaiting presentation"};
    }

    void maintenance() {
        auto event = loader.poll(session);
        if (!event) return;
        if (event->kind == IsfLoadKind::Catalog) {
            for (const auto& warning : event->catalog->warnings()) logWarn(warning);
            playback.setCatalog(event->catalog);
            dispatch();
            return;
        }
        if (!playback.accepts(event->generation)) return;
        if (event->kind == IsfLoadKind::Failed) { fail(event->generation, event->error); return; }
        if (event->kind != IsfLoadKind::Source) return;
        try {
            auto size = dimensions();
            auto candidate = std::make_unique<IsfRuntime>(context, budget, event->source,
                                                         size.first, size.second);
            auto values = std::make_unique<IsfParameters>(event->source->document.inputs);
            values->initializeDefaults(candidate->defaults());
            for (const auto& command : playback.controls()) {
                std::string why;
                if (!values->apply(command, why)) logWarn("ISF: " + why);
            }
            candidate->draw(*values, audio, 0, 0, 0);
            if (!playback.commit(event->generation, event->source->document.id)) return;
            runtime = std::move(candidate);
            parameters = std::move(values);
            lastGoodParameters = std::make_unique<IsfParameters>(*parameters);
            prepared = true;
            dirty = false;
            time = 0;
            frame = 0;
        } catch (const std::exception& e) {
            fail(event->generation, event->source->document.id + ": " + e.what());
        }
    }
};

IsfBackend::IsfBackend(const BackendInit& init, IsfLoader& loader, IsfContext& context, IsfBudget& budget)
    : impl_(std::make_unique<Impl>(init, loader, context, budget)) {}
IsfBackend::~IsfBackend() = default;
PreparationResult IsfBackend::preparation() {
    if (impl_->runtime) return {PreparationState::Ready, {}};
    return {impl_->playback.pending() ? PreparationState::Pending : PreparationState::Failed,
            impl_->playback.error()};
}
void IsfBackend::maintenance() { impl_->maintenance(); }
void IsfBackend::addPcm(const float* samples, size_t frames) {
    impl_->audio.append(samples, frames);
    if (impl_->prepared) impl_->dirty = true;
}
void IsfBackend::clearAudioHistory() {
    impl_->audio.clear();
    if (impl_->prepared) impl_->dirty = true;
}
void IsfBackend::render(float dt) {
    auto& state = *impl_;
    if (!state.runtime) return;
    double elapsed = std::isfinite(dt) ? std::max(0.0, double(dt)) : 0;
    if (state.prepared) {
        bool rejected = false;
        if (state.dirty) {
            try { state.runtime->draw(*state.parameters, state.audio, 0, 0, 0); }
            catch (const IsfFrameRejected& e) {
                logWarn(e.what());
                state.parameters->restoreValues(*state.lastGoodParameters);
                rejected = true;
            }
        }
        state.runtime->present(state.width, state.height);
        state.prepared = false;
        state.dirty = false;
        state.frame = 1;
        // A rejected redraw presents the prepared image, which may already
        // contain queued events. Acknowledge only those, keeping newer events.
        if (rejected) state.parameters->presented(*state.lastGoodParameters);
        else state.parameters->presented();
        *state.lastGoodParameters = *state.parameters;
        state.lastGoodParameters->presented();
        return;
    }
    try { state.runtime->draw(*state.parameters, state.audio, state.time + elapsed, elapsed, state.frame); }
    catch (const IsfFrameRejected& e) {
        logWarn(e.what());
        state.parameters->restoreValues(*state.lastGoodParameters);
        state.runtime->present(state.width, state.height);
        return;
    }
    state.runtime->present(state.width, state.height);
    state.time += elapsed;
    ++state.frame;
    state.parameters->presented();
    *state.lastGoodParameters = *state.parameters;
    state.playback.presented(elapsed);
    state.dispatch();
}
void IsfBackend::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    auto& state = *impl_;
    state.width = width;
    state.height = height;
    if (!state.runtime) return;
    auto size = state.dimensions();
    std::string why;
    if (!state.runtime->update(*state.parameters, size.first, size.second, why, state.prepared))
        logWarn("ISF resize: " + why);
    else if (state.prepared) state.dirty = true;
}
void IsfBackend::setSettings(const PlaybackSettings& settings) { impl_->playback.setSettings(settings); }
void IsfBackend::next() { impl_->playback.navigate(1, false); impl_->dispatch(); }
void IsfBackend::prev() { impl_->playback.navigate(-1, false); impl_->dispatch(); }
void IsfBackend::random() { impl_->playback.navigate(1, true); impl_->dispatch(); }
bool IsfBackend::selectPreset(const std::string& id) {
    bool accepted = impl_->playback.selectPreset(id);
    impl_->dispatch();
    return accepted;
}
bool IsfBackend::selectPlaylist(const std::string& folder) {
    bool accepted = impl_->playback.selectPlaylist(folder);
    impl_->dispatch();
    return accepted;
}
VisualSelection IsfBackend::selection() const { return impl_->playback.selection(); }
CommandResult IsfBackend::setParameter(const cmd::IsfParam& command) { return impl_->control(command); }
CommandResult IsfBackend::triggerEvent(const cmd::IsfEvent& command) { return impl_->control(command); }
}
