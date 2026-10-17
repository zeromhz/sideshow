#include <catch2/catch_test_macros.hpp>
#include "osc_server.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
using namespace sideshow;
TEST_CASE("ISF wire input rejects int64 before narrowing","[isf]") {
 int socketFd=socket(AF_INET,SOCK_DGRAM,0);REQUIRE(socketFd>=0);
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 REQUIRE(bind(socketFd,reinterpret_cast<sockaddr*>(&addr),sizeof addr)==0);
 socklen_t size=sizeof addr;REQUIRE(getsockname(socketFd,reinterpret_cast<sockaddr*>(&addr),&size)==0);
 int port=ntohs(addr.sin_port);close(socketFd);
 CommandQueue q;PlayerStatus status;OscServer server(port,"test",q,status);
 auto destination=lo_address_new("127.0.0.1",std::to_string(port).c_str());REQUIRE(destination);
 lo_send(destination,"/sideshow/test/isf_param","sh","choice",int64_t{4294967298});
 lo_send(destination,"/sideshow/test/isf_param","si","choice",int32_t{4});
 lo_address_free(destination);
 std::optional<Command> result;
 for(int n=0;n<200&&!result;++n){result=q.pop();if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 REQUIRE(result);auto* param=std::get_if<cmd::IsfParam>(&*result);REQUIRE(param);
 REQUIRE(param->values.size()==1);CHECK(std::get<int32_t>(param->values[0])==4);
}

namespace {
struct AckReceiver {
 lo_server socket=lo_server_new(nullptr,nullptr);
 std::vector<std::vector<std::string>> replies;
 AckReceiver(){lo_server_add_method(socket,"/sideshow/ack","ssss",[](const char*,const char*,lo_arg** a,int,lo_message,void* p){
   static_cast<AckReceiver*>(p)->replies.push_back({&a[0]->s,&a[1]->s,&a[2]->s,&a[3]->s});return 0;
 },this);}
 ~AckReceiver(){lo_server_free(socket);}
 void receive(){lo_server_recv_noblock(socket,10);}
};
}
TEST_CASE("OSC acknowledgements wait for application and report effective values") {
 CommandQueue queue;PlayerStatus status;OscServer server(0,"panel-01",queue,status);
 REQUIRE(server.port()>0);AckReceiver receiver;
 auto destination=lo_address_new("127.0.0.1",std::to_string(server.port()).c_str());
 lo_send(destination,"/sideshow/request","sisf","request-1",lo_server_get_port(receiver.socket),"/sideshow/all/master",5.0f);
 receiver.receive();CHECK(receiver.replies.empty());
 for(int n=0;n<100 && receiver.replies.empty();++n){
  queue.processAcknowledgedFrame([](const Command& c){
   REQUIRE(std::holds_alternative<cmd::Master>(c));
   CHECK(std::get<cmd::Master>(c).value==1);
   return CommandResult{"applied","master=1"};
  });receiver.receive();
 }
 REQUIRE(receiver.replies.size()==1);
 CHECK(receiver.replies[0]==std::vector<std::string>{"request-1","panel-01","applied","master=1"});
 lo_address_free(destination);
}
TEST_CASE("OSC acknowledgements reject invalid requests without enqueuing") {
 CommandQueue queue;PlayerStatus status;OscServer server(0,"panel-01",queue,status);
 AckReceiver receiver;auto destination=lo_address_new("127.0.0.1",std::to_string(server.port()).c_str());
 lo_send(destination,"/sideshow/request","sisf","bad",lo_server_get_port(receiver.socket),"/sideshow/all/fluid_persistence",99.0f);
 for(int n=0;n<100&&receiver.replies.empty();++n)receiver.receive();
 REQUIRE(receiver.replies.size()==1);CHECK(receiver.replies[0][2]=="rejected");CHECK_FALSE(queue.pop());
 lo_send(destination,"/sideshow/request","sis","wrong-panel",lo_server_get_port(receiver.socket),"/sideshow/panel-02/next");
 for(int n=0;n<5;++n)receiver.receive();
 CHECK(receiver.replies.size()==1);CHECK_FALSE(queue.pop());
 lo_send(destination,"/sideshow/request","sissh","wide-int",lo_server_get_port(receiver.socket),"/sideshow/all/isf_param","choice",int64_t{4294967298});
 for(int n=0;n<100&&receiver.replies.size()<2;++n)receiver.receive();
 REQUIRE(receiver.replies.size()==2);CHECK(receiver.replies[1][2]=="rejected");CHECK_FALSE(queue.pop());
 lo_address_free(destination);
}
