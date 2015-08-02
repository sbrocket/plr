#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "plr.h"
#include "plrLog.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables & defines

static int g_pintoolMode = 0;
static int g_ldpreloadMode = 1;
static int g_numRedunProc = 3;

///////////////////////////////////////////////////////////////////////////////
// Private functions

void atexit_cleanupShm();
int startFirstProcess(int argc, char **argv);
void printUsage();

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  // Parse command line arguments
  int opt;
  while ((opt = getopt(argc, argv, "pln:")) != -1) {
    switch (opt) {
    case 'h':
      printUsage();
      return 0;
    case 'p':
      g_pintoolMode = 2;
      break;
    case 'l':
      g_ldpreloadMode = 2;
      break;
    case 'n': {
      char *endptr;
      long val = strtol(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
        fprintf(stderr, "Error: Argument for -n is not an integer value\n");
        return 1;
      }
      g_numRedunProc = val;
    } break;
    case '?':
      // getopt() prints an error message to stderr for unrecognized options 
      // if opterr != 0 (default)
      return 1;
    default:
      fprintf(stderr, "Error: Unexpected return value (%d) from getopt()\n", opt);
      return 1;
    }
  }
  
  if (g_pintoolMode == 2) {
    if (g_ldpreloadMode == 2) {
      fprintf(stderr, "Error: -l and -p options are mutually exclusive\n");
      printUsage();
      return 1;
    } else {
      g_ldpreloadMode = 0;
    }
  }
  
  int progArgc = argc-optind;
  if (progArgc < 1) {
    fprintf(stderr, "Error: Specify a command to run using PLR\n");
    printUsage();
    return 1;
  }
  char **progArgv = &argv[optind];
  
  if (g_ldpreloadMode && getenv("LD_PRELOAD") != NULL) {
    fprintf(stderr, "Error: LD_PRELOAD already set when running PLR in LD_PRELOAD mode\n");
    return 1;
  }
  
  if (plr_figureheadInit(g_numRedunProc, g_pintoolMode) < 0) {
    fprintf(stderr, "Error: PLR figurehead init failed\n");
    return 1;
  }
  
  int ret = startFirstProcess(progArgc, progArgv);
  
  // Need to register atexit here to avoid it getting registered
  // for forked children too
  atexit(atexit_cleanupShm);
  
  if (ret < 0) {
    fprintf(stderr, "Error: Failed to launch specified program\n");
    return 1;
  }
  
  while (1) {
    int status;
    int pid = wait(&status);
    if (pid < 0) {
      if (errno == ECHILD) {
        plrlog(LOG_DEBUG, "PLR: All children exited\n");
        break;
      } else {
        perror("wait");
        return 1;
      }
    } else if (WIFEXITED(status)) {
      plrlog(LOG_DEBUG, "Pid %d exited normally with status %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      plrlog(LOG_DEBUG, "Pid %d exited with signal %d\n", pid, WTERMSIG(status));
    } else {
      plrlog(LOG_DEBUG, "Pid %d exited for unknown reason\n", pid);
    }
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

void atexit_cleanupShm() {
  if (plr_figureheadExit() < 0) {
    fprintf(stderr, "Error: PLR figurehead exit failed\n");
  }
}

///////////////////////////////////////////////////////////////////////////////

int startFirstProcess(int argc, char **argv) {  
  // Create a close-on-exec pipe so parent can know if the child
  // launches properly, and receive errno if not
  int execErrPipe[2];
  if (pipe(execErrPipe) < 0) {
    perror("pipe"); exit(1);
  }
  if (fcntl(execErrPipe[1],F_SETFD,FD_CLOEXEC) < 0) {
    perror("fcntl"); exit(1);
  }
  
  int child = fork();
  if (child < 0) {
    perror("fork");
    return -1;
  } else if (child == 0) {
    // Close read end of exec error handling pipe
    close(execErrPipe[0]);
    
    char **cArgv;
    if (g_pintoolMode) {
      // Build child argv by appending pintool injection to front of command
      char *pinArgv[4] = {"pin64", "-t", "lib/pinFaultInject.so", "--"};
      int cArgc = 4+argc+1;
      cArgv = malloc(cArgc*sizeof(char*));
      
      int i = 0;
      for (; i < 4; ++i) {
        cArgv[i] = pinArgv[i];
      }
      for (; i < cArgc; ++i) {
        cArgv[i] = argv[i-4];
      }
      setenv("LD_PRELOAD","./lib/libplrPreload.so",0);
      
    } else {
      // argv is already NULL-terminated
      cArgv = argv;
      
      // Set LD_PRELOAD in environment to replace libc syscall functions
      setenv("LD_PRELOAD","./lib/libplrPreload.so",0);
    }
    
    execvp(cArgv[0], cArgv);
    
    // execvp only returns if it fails
    // If it fails, send errno to parent
    int wrSize = write(execErrPipe[1], &errno, sizeof(errno));
    if (wrSize < 0) {
      perror("write");
    }
    exit(1);
  } else {
    // Wait for exec error handling pipe to close with EOF (indicating
    // successful exec) or to pass an error number through
    close(execErrPipe[1]);
    int childErrno;
    int rdSize = read(execErrPipe[0], &childErrno, sizeof(childErrno));
    close(execErrPipe[0]);
    
    if (rdSize < 0) {
      perror("read"); exit(1);
    } else if (rdSize > 0) {
      // Child process failed to exec, print error and break out of
      // loop, stop starting children
      printf("execvp: %s\n", strerror(childErrno));   
      return -1;
    }
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

void printUsage() {
  printf("\n"
    "Usage: plr [OPTIONS] <program> [program args]\n"
    "Example: plr -l -- cat myfile.txt\n"
    "Options:\n"
    "  -h         Print this help and exit\n"
    "  -l         Enable LD_PRELOAD mode (default)\n"
    "  -p         Enable PinTool mode\n"
    "  -n <int>   Number of redundant processes to create (default=3)\n");
}
