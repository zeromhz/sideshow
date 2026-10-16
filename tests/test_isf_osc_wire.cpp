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
