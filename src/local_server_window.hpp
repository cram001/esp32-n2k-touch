#pragma once
#include <atomic>
#include <cstdint>
// Mutations and deadline reads require the OTA mutex; state reads are atomic.
struct LocalServerWindow {
 enum class State { Stopped, Starting, Listening, Stopping };
 std::atomic<State> state{State::Stopped};
 int64_t deadline=0;
 bool enable(int64_t now,int64_t duration,bool &start) {
  start=false;
  if(state==State::Stopping)return false;
  start=state==State::Stopped;
  deadline=now+duration;
  if(start)state=State::Starting;
  return true;
 }
 bool stop_if_due(int64_t now,bool busy) {
  if(state!=State::Listening)return false;
  if(busy || deadline>now)return false;
  state=State::Stopping;return true;
 }
 void stopped(){deadline=0;state=State::Stopped;}
 bool active()const{return state!=State::Stopped;}
};
