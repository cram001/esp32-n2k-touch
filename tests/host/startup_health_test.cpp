#include <cassert>
#include <cstdio>
#include "local_server_retry.hpp"
int main(){
 using D=StartupHealthDecision;
 assert(startup_health_decision(true,false,true,5000000)==D::Confirm);
 assert(startup_health_decision(false,true,true,5000000)==D::Reject);
 assert(startup_health_decision(true,false,false,4999999)==D::Wait);
 assert(startup_health_decision(true,false,false,25000000)==D::Reject);
 std::puts("PASS: environment errors retain healthy images; image/UI failures reject");
}
