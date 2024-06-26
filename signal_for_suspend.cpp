#ifdef __linux__

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void
_swift_formatUnsigned(unsigned u, char buffer[22])
{
  char *ptr = buffer + 22;
  *--ptr = '\0';
  while (ptr > buffer) {
    char digit = '0' + (u % 10);
    *--ptr = digit;
    u /= 10;
    if (!u)
      break;
  }

  // Left-justify in the buffer
  if (ptr > buffer) {
    char *pt2 = buffer;
    while (*ptr)
      *pt2++ = *ptr++;
    *pt2++ = '\0';
  }
}

/* Find the signal to use to suspend the given thread.

   Sadly, libdispatch blocks SIGUSR1, so we can't just use that everywhere;
   and on Ubuntu 20.04 *something* is starting a thread with SIGPROF blocked,
   so we can't just use that either.

   We also can't modify the signal mask for another thread, since there is
   no syscall to do that.

   As a workaround, read /proc/<pid>/task/<tid>/status to find the signal
   mask so that we can decide which signal to try and send. */
int
signal_for_suspend(int pid, int tid)
{
  char pid_buffer[22];
  char tid_buffer[22];

  _swift_formatUnsigned((unsigned)pid, pid_buffer);
  _swift_formatUnsigned((unsigned)tid, tid_buffer);

  char status_file[6 + 22 + 6 + 22 + 7 + 1];

  strcpy(status_file, "/proc/");    // 6
  strcat(status_file, pid_buffer);  // 22
  strcat(status_file, "/task/");    // 6
  strcat(status_file, tid_buffer);  // 22
  strcat(status_file, "/status");   // 7 + 1 for NUL

  int fd = open(status_file, O_RDONLY);
  if (fd < 0)
    return -1;

  enum match_state {
    Matching,
    EatLine,
    AfterMatch,
    InHex,

    // states after this terminate the loop
    Done,
    Bad
  };

  enum match_state state = Matching;
  const char *toMatch = "SigBlk:";
  const char *matchPtr = toMatch;
  char buffer[256];
  uint64_t mask = 0;
  ssize_t count;
  while (state < Done && (count = read(fd, buffer, sizeof(buffer))) > 0) {
    char *ptr = buffer;
    char *end = buffer + count;

    while (state < Done && ptr < end) {
      int ch = *ptr++;

      switch (state) {
      case Matching:
        if (ch != *matchPtr) {
          state = EatLine;
          matchPtr = toMatch;
        } else if (!*++matchPtr) {
          state = AfterMatch;
        }
        break;
      case EatLine:
        if (ch == '\n')
          state = Matching;
        break;
      case AfterMatch:
        if (ch == ' ' || ch == '\t') {
          break;
        }
        state = InHex;
        // SWIFT_FALLTHROUGH;
      case InHex:
        if (ch >= '0' && ch <= '9') {
          mask = (mask << 4) | (ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
          mask = (mask << 4) | (ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
          mask = (mask << 4) | (ch - 'A' + 10);
        } else if (ch == '\n') {
          state = Done;
          break;
        } else {
          state = Bad;
        }
        break;
      case Done:
      case Bad:
        break;
      }
    }
  }

  close(fd);

  if (state == Done) {
    if (!(mask & (1 << (SIGUSR1 - 1))))
      return SIGUSR1;
    else if (!(mask & (1 << (SIGUSR2 - 1))))
      return SIGUSR2;
    else if (!(mask & (1 << (SIGPROF - 1))))
      return SIGPROF;
    else
      return -1;
  }

  return -1;
}

void *th_func(void *arg) {
    int pid = getpid();
    int tid = gettid();
    int signal = signal_for_suspend(pid, tid);
    printf("signal: %d\n", signal);
    return NULL;
}

int main() {
    pthread_t th;
    for (int i = 0; i<100; i++) {
        if (pthread_create(&th, NULL, th_func, NULL) != 0) {
            printf("pthread_create failed\n");
            return 1;
        }
        if (pthread_join(th, NULL) != 0) {
            printf("pthread_join failed\n");
            return 2;
        }
    }
    return 0;
}

#endif