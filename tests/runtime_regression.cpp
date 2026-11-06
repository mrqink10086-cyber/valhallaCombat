#include "include/CombatRuntime.h"
#include "include/offsets.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
void check(bool result,const char* label) { if (!result) throw std::runtime_error(label); std::cout<<"PASS "<<label<<'\n'; }
bool equal(float a,float b) { return std::abs(a-b)<0.0001f; }
int main()
{
 try {
  auto& runtime=CombatRuntime::get(); runtime.initialize(); runtime.beginSession();
  check(runtime.ready(),"owner session ready");
  bool wrong=false; std::thread worker([&]{ wrong=runtime.ready(); }); worker.join();
  check(!wrong,"worker cannot mutate owner state");
  int calls=0; CombatRuntime::Ticket cancelled=0;
  runtime.schedule(0,[&]{ runtime.cancel(cancelled); ++calls; }); cancelled=runtime.schedule(0,[&]{ calls+=100; }); runtime.update(false);
  check(calls==1,"cancellation suppresses already-due task");
  runtime.schedule(0,[]{ throw std::runtime_error("controlled failure"); }); runtime.schedule(0,[&]{ ++calls; }); runtime.update(false);
  check(calls==2,"failed callback does not stop later requests");
	  int afterUnknown=0;
	  runtime.schedule(0,[]{ throw 7; }); runtime.schedule(0,[&]{ ++afterUnknown; }); runtime.update(false);
	  check(afterUnknown==1,"non-standard exception does not strand due callbacks");
  runtime.schedule(0.05f,[&]{ ++calls; }); Sleep(80); runtime.update(true); Sleep(20); runtime.update(false);
  check(calls==2,"paused wall time excluded"); Sleep(60); runtime.update(false); check(calls==3,"active delay eventually expires");
  runtime.control([&]{ ++calls; }); runtime.update(true); SKSE::GetTaskInterface()->drain();
  check(calls==4,"control applies while combat paused");
  RE::Offset::speed=0.4f; runtime.slowTime(0.03f,0.1f); runtime.slowTime(0.15f,0.3f);
  check(equal(RE::Offset::speed,0.1f),"strongest overlapping slow time selected");
  Sleep(60); runtime.update(false); check(equal(RE::Offset::speed,0.3f),"earlier expiry preserves later slow time");
  Sleep(180); runtime.update(false); check(equal(RE::Offset::speed,0.4f),"original multiplier restored");
  runtime.slowTime(1,0.2f); RE::Offset::speed=0.7f; runtime.update(false); runtime.clearSlowTime();
  check(equal(RE::Offset::speed,0.7f),"external multiplier preserved");
  runtime.schedule(0,[&]{ calls+=100; }); runtime.control([&]{ calls+=100; }); const auto old=runtime.session();
  runtime.endSession(); runtime.beginSession(); runtime.update(false); SKSE::GetTaskInterface()->drain();
  check(runtime.session()!=old && calls==4,"new session cancels timer and queued control");
	  int nested=0;
	  runtime.schedule(0,[&]{ ++nested; runtime.schedule(0,[&]{ ++nested; }); });
	  runtime.update(false); check(nested==1,"nested zero-delay work waits for next update");
	  runtime.update(false); check(nested==2,"nested zero-delay work executes next update");
	  constexpr int count=8192;
	  std::thread producer([&]{ for (int i=0;i<count;++i) { auto ticket=runtime.schedule(0,[&]{ ++nested; }); if (i%2==0) runtime.cancel(ticket); } });
	  producer.join(); runtime.update(false);
	  check(nested==2+count/2,"dense worker submissions and cancellation execute once");
		  std::vector<int> order;
		  runtime.schedule(.001f,[&]{ order.push_back(1); });
		  runtime.schedule(0,[&]{ order.push_back(2); });
		  Sleep(5); runtime.update(false);
		  check(order==std::vector<int>{1,2},"due callbacks preserve submission order across deadlines");
		  int newSessionCalls=0;
		  runtime.schedule(0,[&]{ runtime.endSession(); runtime.beginSession(); runtime.schedule(0,[&]{ ++newSessionCalls; }); });
		  runtime.schedule(0,[&]{ newSessionCalls+=100; });
		  runtime.update(false); check(newSessionCalls==0,"session replacement discards remaining old due callbacks");
		  runtime.update(false); check(newSessionCalls==1,"new-session callback waits for next outer update");
  runtime.endSession(); check(!runtime.ready() && runtime.schedule(0,[]{})==0,"inactive session rejects combat requests");
  return 0;
 } catch (const std::exception& exception) { std::cerr<<"FAIL "<<exception.what()<<'\n'; return 1; }
}
