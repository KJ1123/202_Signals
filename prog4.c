#include <stdio.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <unistd.h> 
#include <stdlib.h>
#include <signal.h>
#include <wait.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define N 4096

/* Global variable shared between the main process and the signal handler */
volatile int sum = 0;
volatile int message_count = 0;
volatile int sending_count = 0;
volatile int parent_caught = 0;
sig_atomic_t parent_added = 0;
sig_atomic_t sigusr2_received = 0;
sig_atomic_t handler_installed = 0;

/* function to initialize an array of integers */
void initialize(int*);
/* Wrapper functions for all system calls */
void unix_error(const char *msg);
pid_t Fork();
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *status, int options);
int Sigqueue(pid_t pid, int signum, union sigval value);
int Sigemptyset(sigset_t *set);
int Sigfillset(sigset_t *set);
int Sigaction(int signum, const struct sigaction *new_act, struct sigaction *old_act);
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
ssize_t Write(int d, const void *buffer, size_t nbytes);
ssize_t Read(int d, void *buffer, size_t nbytes);
typedef void handler_t;
handler_t *Signal(int signum, handler_t *handler);

void sigusr2_handler(int sig, siginfo_t *value, void *ucontext) {
  sigset_t mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, NULL);
  int psum = value->si_value.sival_int;
  sum += psum;
  sigusr2_received = 1;
  
  Sigprocmask(SIG_UNBLOCK, &mask, NULL);

  if (!handler_installed) {
    handler_installed = 1;
    printf("Parent process %d installing SIGUSR2 handler %d\n", getpid(), sum);
  }
}

/* main function */
int main() {
  int A[N];
  initialize(A);
  /* install the SIGUSR2 handler using Signal (portable handler) */ 
  Signal(SIGUSR2, sigusr2_handler);
  
  /* create (P-1) processes (macro P defined at compile time) */
  /* make each process send the partial sum to the parent process using sigqueue with signal SIGUSR2 and exit after that */
  /* reap the child processes */
  pid_t pid[P];
  int i;
  for (i = 0; i < P - 1; i++) {
    // Calculate the index range for this child process
    int start = i * N / P;
    int end = (i + 1) * N / P - 1;

    // Create the child process
    pid[i] = Fork();
    if (pid[i] == 0) {
      // Child process code
      int partial_sum = 0;
      for (int j = start; j <= end; j++) {
          partial_sum += A[j];
      }
      if (message_count < P) {
        printf("Child process %d adding the elements from index %d to %d\n", getpid(), start, end);
        printf("Child process %d sending SIGUSR2 to parent process with the partial sum %d\n", getpid(), partial_sum);
      }
      message_count++;
      union sigval value;
      value.sival_int = partial_sum;
      Sigqueue(getppid(), SIGUSR2, value);
      exit(0);
    }
  }

  // Parent process code
  pid_t wpid;
  int status;
  for (int j = 0; j < P - 1; j++) {
    wpid = Wait(&status);
    if (WIFEXITED(status)) {
      printf("Child process %d terminated normally with exit status %d\n", wpid, WEXITSTATUS(status));
    } else {
      printf("Child process %d terminated abnormally\n", wpid);
    }
  }

  // Compute the partial sum for the parent process
  int start = i * N / P;
  int end = N - 1;
  int partial_sum = 0;
  for (int j = start; j <= end; j++) {
    partial_sum += A[j];
    //union sigval value;
    //value.sival_int = partial_sum;
    //Sigqueue(getpid(), SIGUSR2, value);
    if (!parent_added) {
      parent_added = 1;
      printf("Parent process %d adding the elements from index %d to %d\n", getpid(), start, end);
    }

    if (parent_caught < P - 1) {
      printf("Parent process caught SIGUSR2 with partial sum: %d\n", partial_sum);
    }
    parent_caught++;
  }

  // Wait for the final SIGUSR2 signal to be received and processed
  while (!sigusr2_received) {
    pause();
  }

  sum += partial_sum;

  /* print the final sum */
  printf("Final sum = %d\n", sum);
  exit(0);
}

void initialize(int M[N]) {
  int i;
  for(i = 0; i < N; i++){
    M[i] = i + 1;
  }
}

/* Definition of the wrapper system call functions */
void unix_error(const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

pid_t Fork() {
  pid_t pid_return;

  if ((pid_return = fork()) < 0)
    unix_error("Fork error");
  return pid_return;
}

pid_t Wait(int *status) {
  pid_t pid_return;

  if ((pid_return  = wait(status)) < 0)
    unix_error("Wait error");
  return pid_return;
}

pid_t Waitpid(pid_t pid, int *status, int options) {
  pid_t pid_return;

  if ((pid_return  = waitpid(pid, status, options)) < 0) 
    unix_error("Waitpid error");
  return(pid_return);
}

int Sigqueue(pid_t pid, int signum, union sigval value) {
  int result;
  result = sigqueue(pid, signum, value);
  if (result == -1) {
    unix_error("Sigqueue error");
  }
  return result;
}

int Sigemptyset(sigset_t *set) {
  if (sigemptyset(set) < 0) {
	  unix_error("Sigemptyset error");
  }
  return 0;
}

int Sigfillset(sigset_t *set) {
  if (sigfillset(set) < 0) {
	  unix_error("Sigfillset error");
  }
  return 0;
}

int Sigaction(int signum, const struct sigaction *new_act, struct sigaction *old_act) {
  if (sigaction(signum, new_act, old_act) < 0) {
    unix_error("Sigaction error");
  }
  return 0;
}

int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  if (sigprocmask(how, set, oldset) < 0) {
	  unix_error("Sigprocmask error");
  }
  return 0;
}

ssize_t Write(int d, const void *buffer, size_t nbytes) {
  ssize_t result;

  if ((result = write(d, buffer, nbytes)) < 0) {
	  unix_error("Write error");
  }
  return result;
}

ssize_t Read(int d, void *buffer, size_t nbytes) {
  ssize_t results;

  if ((results = read(d, buffer, nbytes)) < 0) {
	  unix_error("Read error");
  }
  return results;
}

handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction curr, old;

  curr.sa_handler = handler;  
  sigemptyset(&curr.sa_mask); /* Block sigs of type being handled */
  curr.sa_flags = SA_RESTART; /* Restart syscalls if possible */

  if (sigaction(signum, &curr, &old) < 0) {
    unix_error("Signal error");
  }
  return (old.sa_handler);
}

