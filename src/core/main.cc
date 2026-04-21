// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_manager.h"

#include <signal.h>

static ac::ActorManager manager;

void sigal_handle(int sig_num) {
  (void)sig_num;
  manager.Shutdown();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  struct sigaction act;
  act.sa_handler = sigal_handle;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGTERM, &act, NULL);
  sigaction(SIGINT, &act, NULL);

  manager.LoadModules();
  manager.EventLoop();
  return 0;
}
