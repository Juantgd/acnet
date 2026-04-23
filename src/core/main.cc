// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_manager.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "version.h"

static ac::ActorManager manager;

namespace {
constexpr static const char *kControlFile = "logs/acnet.control";
constexpr static const char *kPidFile = "logs/acnet.pid";

static const char short_options[] = ":hr:s";

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"reload", optional_argument, NULL, 'r'},
    {"stop", no_argument, NULL, 's'},
    {0, 0, 0, 0}};

} // namespace

static bool mkdir_if_not_exists(const char *dir) {
  struct stat s;
  if (stat(dir, &s) == 0) {
    if (S_ISDIR(s.st_mode)) {
      return true;
    } else {
      return false;
    }
  }
  if (mkdir(dir, 0755)) {
    fprintf(stderr, "mkdir() failed, error: %s\n", strerror(errno));
    return false;
  }
  return true;
}

static void delete_if_exists(const char *filepath) {
  struct stat s;
  if (stat(filepath, &s)) {
    fprintf(stderr, "stat() failed, error: %s\n", strerror(errno));
    return;
  }
  if (!S_ISREG(s.st_mode)) {
    fprintf(stderr, "not regular file.");
    return;
  }
  if (unlink(filepath) == -1) {
    fprintf(stderr, "unlink() failed, error: %s\n", strerror(errno));
  }
}

static void print_usage(const char *file_name) {
  fprintf(stdout,
          "Usage: %s [options]\n"
          "Version: %s\n\n"
          "Options:\n"
          "-h | --help                Print this message\n"
          "-r | --reload module       Reload the specified module, all to "
          "reload all modules\n"
          "-s | --stop                Stop the service running\n"
          "",
          file_name, ACNET_VERSION);
}

static int parse_command_line(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'h': {
      print_usage(argv[0]);
      return 0;
    }
    case 'r':
    case 's': {
      if (mkdir_if_not_exists("logs")) {
        FILE *control_file = fopen(kControlFile, "w");
        if (opt == 'r') {
          fprintf(control_file, "reload %s", optarg);
        } else {
          fprintf(control_file, "stop");
        }
        fclose(control_file);
        char pid_str[32];
        FILE *pid_file = fopen(kPidFile, "r");
        if (pid_file == NULL) {
          fprintf(stderr, "fopen() failed, error: %s\n", strerror(errno));
          return -1;
        }
        fgets(pid_str, 32, pid_file);
        fclose(pid_file);
        pid_t acnet_pid = static_cast<pid_t>(strtol(pid_str, NULL, 10));
        kill(acnet_pid, SIGHUP);
      } else {
        exit(EXIT_FAILURE);
      }
      return 0;
    }
    case ':': {
      fprintf(stderr, "选项 -%c 缺少必选参数\n", optopt);
      return -1;
    }
    case '?': {
      fprintf(stderr, "未知选项: -%c\n", optopt);
      return -1;
    }
    }
  }
  return 0;
}

static void sigal_handle(int sig_num) {
  (void)sig_num;
  char cmd[32];
  FILE *control_file = fopen(kControlFile, "r");
  if (control_file) {
    fgets(cmd, 32, control_file);
    fclose(control_file);
    delete_if_exists(kControlFile);
    char *pos = strchr(cmd, ' ');
    if (pos != NULL) {
      *pos = '\0';
      if (strcmp(cmd, "reload") == 0) {
        manager.ReloadModule(pos + 1);
      }
    } else if (strcmp(cmd, "stop") == 0) {
      manager.Shutdown();
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    if (parse_command_line(argc, argv)) {
      print_usage(argv[0]);
    }
    return 0;
  }

  if (!mkdir_if_not_exists("logs")) {
    fprintf(stderr, "failed to create directory: ./logs/");
    exit(EXIT_FAILURE);
  }

  FILE *pid_file = fopen(kPidFile, "w");
  if (pid_file == NULL) {
    fprintf(stderr, "fopen() failed, error: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  fprintf(pid_file, "%d", getpid());
  fclose(pid_file);

  struct sigaction act;
  act.sa_handler = sigal_handle;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGHUP, &act, NULL);

  manager.EventLoop();

  delete_if_exists(kPidFile);

  return 0;
}
