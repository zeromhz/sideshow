#include "osc_server.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "osc_parse.h"

namespace sideshow {

OscServer::OscServer(int port, std::string panelId, CommandQueue& queue, const PlayerStatus& status)
    : panelId_(std::move(panelId)), queue_(queue), status_(status) {
    const std::string portString = std::to_string(port);
    server_ = lo_server_thread_new(portString.c_str(), &OscServer::onError);
    if (!server_) {
        logError("OSC: cannot listen on UDP port " + portString + "; remote control disabled");
        return;
    }
    lo_server_thread_add_method(server_, nullptr, nullptr, &OscServer::handle, this);
    lo_server_thread_start(server_);
    logInfo("OSC: listening on UDP " + portString + " for /sideshow/all/* and /sideshow/" +
            panelId_ + "/*");
}

OscServer::~OscServer() {
    if (server_) {
        lo_server_thread_stop(server_);
        lo_server_thread_free(server_);
    }
}

int OscServer::handle(const char* path, const char* types, lo_arg** argv, int argc,
                      lo_message message, void* userData) {
    auto* self = static_cast<OscServer*>(userData);
    if (std::strcmp(path, kPingAddress) == 0) {
        self->replyPong(message);
        return 0;
    }

    const std::string address(path);
    const auto suffix=address.substr(address.find_last_of('/')+1);
    if(suffix=="isf_param" || suffix=="isf_event") {
        for(int i=0;i<argc;++i) if(types[i]!=LO_STRING && types[i]!=LO_INT32 && types[i]!=LO_FLOAT) return 0;
    }
    std::vector<OscArg> args;
    for (int i = 0; i < argc; ++i) {
        switch (types[i]) {
            case LO_INT32: args.emplace_back(argv[i]->i); break;
            case LO_FLOAT: args.emplace_back(argv[i]->f); break;
            case LO_DOUBLE: args.emplace_back(static_cast<float>(argv[i]->d)); break;
            case LO_INT64: args.emplace_back(static_cast<int32_t>(argv[i]->h)); break;
            case LO_TRUE: args.emplace_back(int32_t{1}); break;
            case LO_FALSE: args.emplace_back(int32_t{0}); break;
            case LO_STRING: args.emplace_back(std::string(&argv[i]->s)); break;
            case LO_SYMBOL: args.emplace_back(std::string(&argv[i]->S)); break;
            default:
                logDebug(std::string("OSC: unsupported type '") + types[i] + "' in " + path);
                return 0;
        }
    }

    if (auto command = parseOscMessage(self->panelId_, path, args)) {
        self->queue_.push(std::move(*command));
    } else {
        logDebug(std::string("OSC: ignored ") + path + " ," + types);
    }
    return 0;
}

void OscServer::replyPong(lo_message message) {
    lo_address source = lo_message_get_source(message);
    std::string preset;
    float fps = 0.0f;
    status_.get(preset, fps);
    lo_send_from(source, lo_server_thread_get_server(server_), LO_TT_IMMEDIATE, kPongAddress, "ssf",
                 panelId_.c_str(), preset.c_str(), fps);
}

void OscServer::onError(int number, const char* message, const char* where) {
    logError("OSC error " + std::to_string(number) + ": " + (message ? message : "") +
             (where ? std::string(" (") + where + ")" : std::string()));
}

}  // namespace sideshow
