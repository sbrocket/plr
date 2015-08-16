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

typedef enum {
  PINTOOL_MODE_OFF,
  PINTOOL_MODE_TRACE,
  PINTOOL_MODE_INS
} PINTOOL_MODE;

static PINTOOL_MODE g_pintoolMode = PINTOOL_MODE_OFF;
static int g_numRedunProc = 3;
long g_injectEventMean = 0;
char *g_injectEventMeanStr = NULL;

///////////////////////////////////////////////////////////////////////////////
// Private functions

void atexit_cleanupShm();
int startFirstProcess(int argc, char **argv);
void printUsage();

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  long watchdogTimeout = 200;
  char *outputFile = NULL;
  char *errorFile = NULL;
  
  // Parse command line arguments
  int opt;
  while ((opt = getopt(argc, argv, "hp:m:n:t:o:e:")) != -1) {
    switch (opt) {
    case 'h':
      printUsage();
      return 0;
    case 'p':
      if (strcmp(optarg, "t") == 0 || strcmp(optarg, "trace") == 0) {
        g_pintoolMode = PINTOOL_MODE_TRACE;
      } else if (strcmp(optarg, "i") == 0 || strcmp(optarg, "ins") == 0) {
        g_pintoolMode = PINTOOL_MODE_INS;
      } else {
        fprintf(stderr, "Error: Invalid pintool mode for -p, must be \"trace\" or \"ins\"\n");
        return 1;
      }
      break;
     case 'm': {
      char *endptr;
      long val = strtol(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
        fprintf(stderr, "Error: Argument for -m is not an integer value\n");
        return 1;
      }
      g_injectEventMeanStr = optarg;
      g_injectEventMean = val;
    } break;
    case 'n': {
      char *endptr;
      long val = strtol(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
        fprintf(stderr, "Error: Argument for -n is not an integer value\n");
        return 1;
      }
      g_numRedunProc = val;
    } break;
    case 't': {
      char *endptr;
      long val = strtol(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
        fprintf(stderr, "Error: Argument for -t is not an integer value\n");
        return 1;
      }
      watchdogTimeout = val;
    } break;
    case 'o':
      outputFile = optarg;
      break;
    case 'e':
      errorFile = optarg;
      break;
    case '?':
      // getopt() prints an error message to stderr for unrecognized options 
      // if opterr != 0 (default)
      return 1;
    default:
      fprintf(stderr, "Error: Unexpected return value (%d) from getopt()\n", opt);
      return 1;
    }
  }
  
  int progArgc = argc-optind;
  if (progArgc < 1) {
    fprintf(stderr, "Error: Specify a command to run using PLR\n");
    printUsage();
    return 1;
  }
  char **progArgv = &argv[optind];
  
  if (getenv("LD_PRELOAD") != NULL) {
    fprintf(stderr, "Error: LD_PRELOAD already set when running PLR\n");
    return 1;
  }
  
  // If "-o <file>" option was given, open the given file and replace stdout
  if (outputFile != NULL) {
    int outFD = open(outputFile, O_WRONLY | O_TRUNC | O_CREAT, 0660);
    if (outFD == -1) {
      fprintf(stderr, "Error: File argument to -o option could not be opened/created\n");
      return 1;
    }
    if (dup2(outFD, STDOUT_FILENO) < 0) {
      fprintf(stderr, "Error: dup2 failed when replacing stdout\n");
      return 1;
    }
    close(outFD);
  }
  
  // If "-e <file>" option was given, open the given file and replace stderr
  if (errorFile != NULL) {
    int errFD = open(errorFile, O_WRONLY | O_TRUNC | O_CREAT, 0660);
    if (errFD == -1) {
      fprintf(stderr, "Error: File argument to -o option could not be opened/created\n");
      return 1;
    }
    if (dup2(errFD, STDERR_FILENO) < 0) {
      fprintf(stderr, "Error: dup2 failed when replacing stdout\n");
      return 1;
    }
    close(errFD);
  }
  
  int figPid = getpid();
  if (plr_figureheadInit(g_numRedunProc, g_pintoolMode, figPid, watchdogTimeout) < 0) {
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
    if (g_pintoolMode != PINTOOL_MODE_OFF) {
      // Build child argv by appending pintool injection to front of command
      char *pinArgv[] = {"pin64", "-t", "lib/pinFaultInject.so"};
      int pinArgc = sizeof(pinArgv)/sizeof(*pinArgv);
      int maxArgc = pinArgc+4+argc+1;
      cArgv = malloc(maxArgc*sizeof(char*));
      
      // Add pin command/arguments into execvp argv
      int i = 0;
      for (; i < pinArgc; ++i) {
        cArgv[i] = pinArgv[i];
      }
      if (g_pintoolMode == PINTOOL_MODE_TRACE) {
        cArgv[i] = "-trace"; ++i;
        if (g_injectEventMean > 0) {
          cArgv[i] = "-traceMean"; ++i;
          cArgv[i] = g_injectEventMeanStr; ++i;
        }
      } else {
        cArgv[i] = "-ins"; ++i;
         if (g_injectEventMean > 0) {
          cArgv[i] = "-insMean"; ++i;
          cArgv[i] = g_injectEventMeanStr; ++i;
        }
      }
      cArgv[i] = "--"; ++i;
      
      // Copy target program argv into execvp argv
      for (int j=0; j < argc; ++j, ++i) {
        cArgv[i] = argv[j];
      }
      
      // Add NULL terminator to execvp argv
      cArgv[i] = NULL;
    } else {
      // argv is already NULL-terminated
      cArgv = argv;
    }
    
     // Set LD_PRELOAD in environment to replace libc syscall functions
    setenv("LD_PRELOAD","./lib/libplrPreload.so",0);
    
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
    "Example: plr -- cat myfile.txt\n"
    "Options:\n"
    "  -h             Print this help and exit\n"
    "  -o <file>      Redirect stdout to the given file, which is created or overwritten\n"
    "  -e <file>      Redirect stderr to the given file, which is created or overwritten\n"
    "  -p <trace|ins> Apply fault injection Pintool in either trace or instruction mode\n"
    "  -m <long>      Mean number of trace/instructions before fault injection\n"
    "  -t <int>       Watchdog timeout interval, in ms (default=200ms)\n"
    "  -n <int>       Number of redundant processes to create (default=3)\n");
}
