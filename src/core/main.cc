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

static const char short_options[] = ":hs:";

static const struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                             {"signal", no_argument, NULL, 's'},
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
    fprintf(stderr, "not regular file.\n");
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
          "-s | --signal              Send signal to the service, stop | "
          "reload [module] | remove module\n"
          "",
          file_name, ACNET_VERSION);
}

static int parse_command_line(int argc, char *argv[]) {
  int opt;
  const char *command = nullptr;
  while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'h': {
      print_usage(argv[0]);
      return 0;
    }
    case 's': {
      command = optarg;
      break;
    }
    case ':': {
      fprintf(stderr, "option -%c required arguments\n", optopt);
      return -1;
    }
    case '?': {
      fprintf(stderr, "unknown arguments: -%c\n", optopt);
      return -1;
    }
    }
  }
  if (command) {
    if (mkdir_if_not_exists("logs")) {
      FILE *control_file = fopen(kControlFile, "w");
      if (control_file) {
        if (strcmp(command, "stop") == 0) {
          fprintf(control_file, "stop");
        } else if (strcmp(command, "reload") == 0) {
          if (optind < argc) {
            fprintf(control_file, "reload %s", argv[optind]);
          } else {
            fprintf(control_file, "reload all");
          }
        } else if (strcmp(command, "remove") == 0) {
          if (optind < argc) {
            fprintf(control_file, "remove %s", argv[optind]);
          } else {
            fclose(control_file);
            fprintf(stderr,
                    "command remove required an argument as module name\n");
            return -1;
          }
        } else {
          fclose(control_file);
          fprintf(stderr, "unsupported command: %s\n", command);
          return -1;
        }
        fclose(control_file);
        char pid_str[32];
        FILE *pid_file = fopen(kPidFile, "r");
        if (pid_file == NULL) {
          fprintf(stderr, "pid file: fopen() failed, error: %s\n",
                  strerror(errno));
          exit(EXIT_FAILURE);
        }
        if (fgets(pid_str, 32, pid_file) == NULL) {
          if (ferror(pid_file)) {
            fprintf(stderr, "pid file: fgets() failed, error: %s\n",
                    strerror(errno));
          } else {
            fprintf(stderr, "pid file: fgets() failed, empty file\n");
          }
          fclose(pid_file);
          exit(EXIT_FAILURE);
        }
        fclose(pid_file);
        pid_t acnet_pid = static_cast<pid_t>(strtol(pid_str, NULL, 10));
        kill(acnet_pid, SIGHUP);
      } else {
        fprintf(stderr, "control file: fopen() failed. error: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
      }
    } else {
      exit(EXIT_FAILURE);
    }
  }
  return 0;
}

static void sigal_handle(int sig_num) {
  (void)sig_num;
  char cmd[64];
  FILE *control_file = fopen(kControlFile, "r");
  if (control_file) {
    if (fgets(cmd, 64, control_file) == NULL) {
      fclose(control_file);
      return;
    }
    fclose(control_file);
    delete_if_exists(kControlFile);
    char *pos = strchr(cmd, ' ');
    if (pos != NULL) {
      *pos = '\0';
      if (strcmp(cmd, "reload") == 0) {
        manager.ReloadModule(pos + 1);
      } else if (strcmp(cmd, "remove") == 0) {
        manager.RemoveModule(pos + 1);
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
    fprintf(stderr, "failed to create directory: ./logs/\n");
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
