#include "actor_manager.h"

int main(int argc, char *argv[]) {
  ac::ActorManager manager;
  manager.EventLoop();
  return 0;
}
