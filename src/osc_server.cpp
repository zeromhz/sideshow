#include "osc_server.h"

#include <cstring>
#include <cmath>
#include <vector>

#include "log.h"
#include "osc_parse.h"

namespace sideshow {

OscServer::OscServer(int port, std::string panelId, CommandQueue& queue, const PlayerStatus& status)
    : panelId_(std::move(panelId)), queue_(queue), status_(status) {
    const std::string portString = std::to_string(port);
    server_ = lo_server_thread_new(port == 0 ? nullptr : portString.c_str(), &OscServer::onError);
    if (!server_) {
        logError("OSC: cannot listen on UDP port " + portString + "; remote control disabled");
        return;
    }
    lo_server_thread_add_method(server_, nullptr, nullptr, &OscServer::handle, this);
    lo_server_thread_start(server_);
    logInfo("OSC: listening on UDP " + portString + " for /sideshow/all/* and /sideshow/" +
            panelId_ + "/*");
}

int OscServer::port() const {
    return server_ ? lo_server_get_port(lo_server_thread_get_server(server_)) : 0;
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
    if (std::strcmp(path, "/sideshow/request") == 0) {
        self->handleRequest(types, argv, argc, message);
        return 0;
    }
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

void OscServer::handleRequest(const char* types, lo_arg** argv, int argc, lo_message message) {
    // Opt-in envelope: request ID, explicit reply port, original command path, arguments.
    // A valid routing header is required before any response can be addressed safely.
    if(argc<3 || types[0]!=LO_STRING || types[1]!=LO_INT32 || types[2]!=LO_STRING) return;
    const std::string id(&argv[0]->s), path(&argv[2]->s);
    const int replyPort=argv[1]->i;
    if(id.empty() || id.size()>64 || replyPort<1 || replyPort>65535 || path.size()>1024) return;
    const std::string all="/sideshow/all/", mine="/sideshow/"+panelId_+"/";
    if(path.compare(0,all.size(),all)!=0 && path.compare(0,mine.size(),mine)!=0) return;
    auto source=lo_message_get_source(message);
    if(!source || !lo_address_get_hostname(source)) return;
    const std::string host=lo_address_get_hostname(source), port=std::to_string(replyPort), panel=panelId_;
    // This callback is consumed while OscServer is alive by the main render loop,
    // or synchronously on the receiver thread for rejected/superseded requests.
    auto socket=lo_server_thread_get_server(server_);
    auto reply=[host,port,panel,id,socket](const CommandResult& result) {
        auto destination=lo_address_new(host.c_str(),port.c_str());
        if(!destination)return;
        lo_send_from(destination,socket,LO_TT_IMMEDIATE,"/sideshow/ack","ssss",
                     id.c_str(),panel.c_str(),result.status.c_str(),result.detail.substr(0,512).c_str());
        lo_address_free(destination);
    };
    if(argc>8){reply({"rejected","Too many command arguments"});return;}
    std::vector<OscArg> args;
    for(int i=3;i<argc;++i) {
        if(types[i]==LO_INT32) args.emplace_back(argv[i]->i);
        else if(types[i]==LO_FLOAT && std::isfinite(argv[i]->f)) args.emplace_back(argv[i]->f);
        else if(types[i]==LO_STRING && std::strlen(&argv[i]->s)<=512) args.emplace_back(std::string(&argv[i]->s));
        else {reply({"rejected","Arguments must be int32, finite float32, or strings up to 512 bytes"});return;}
    }
    auto command=parseOscMessage(panelId_,path,args);
    if(!command){reply({"rejected","Unknown command or invalid arguments"});return;}
    queue_.push(std::move(*command),std::move(reply));
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
