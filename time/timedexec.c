#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//graceful handle
volatile sig_atomic_t stop = 0;

//signal handle
void handle_sigint(int sig){
    printf("\nSignal - Exiting program via SIGINT\n");
    exit(0);
}

void handle_sigtstp(int sig){
    stop = 1;
    printf("\nSignal - Pausing . Use 'fg' to resume.\n");
    fflush(stdout);
}

void handle_sigcont(int sig){
    stop = 0;
    printf("[Signal - resume.\n");
    fflush(stdout);
}

//for counting threads in processor - discard. but can use /proc/[pid]/status
int thread_count(pid_t pid){
    // This function is included as requested but not currently used in the main logic.
    char path[100];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    int fd = open(path, O_RDONLY);
    if(fd < 0){ return -1; }

    char buff[5000];
    int n = read(fd, buff, sizeof(buff)-1);
    close(fd); // Close the file descriptor
    if (n <= 0) return -1;

    buff[n] = '\0';
    char *line = strstr(buff, "Threads:");
    if(!line) return -1;
   
    line += 8;
    while(*line == ' ' || *line == '\t') line++;

    return atoi(line);
}


//long get_current_rss_kb(pid_t pid) { // **NEW**
//    char path[100];
//    snprintf(path, sizeof(path), "/proc/%d/status", pid);

//    FILE *file = fopen(path, "r");
//    if (!file) {
//        return 0;
//    }

//    char line[256];
//    long rss = 0;
//    while (fgets(line, sizeof(line), file)) {
//        if (strncmp(line, "VmRSS:", 6) == 0) { //can also use VmSize
//            char *p = line + 6;
//            while (*p == ' ' || *p == '\t') p++;
//            rss = atol(p);
//            break;
//        }
//    }
//    fclose(file);
//    return rss;
//}
long get_current_rss_kb(pid_t pid){
  char path[50];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  
  int fd = open(path, O_RDONLY);
  if(fd == -1){
  return 0;
  }
  
  char buffer[3000];
  ssize_t bytes_read;
  long rss = 0;
  
  bytes_read = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  
  if (bytes_read > 0){
    buffer[bytes_read] = '\0';
    
    char *line = strstr(buffer, "VmRSS:");
    
    if (line) {
      char *p = line+6;
      while(*p == ' ' || *p == '\t')p++;
      rss = atol(p);
      }
      }
      return rss;
      }


int main(int argc, char *argv[]) {
    //handle signal - credit codevault
    //one command for exiting, one for pause, one for continue
   

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    //Ctrl c - end
    sa.sa_handler = &handle_sigint;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    //ctrl+z  pause
    sa.sa_handler = &handle_sigtstp;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa, NULL);

    //fg - continue
    sa.sa_handler = &handle_sigcont;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, NULL);



    int max_cpu = 0;      
    int max_clock = 0;    
    long max_mem = 0;  
    char **cmd_child = NULL;


   
//for loop needs to skip first program cmd line (argv[0])
//either stop before no more arguements (argc) or stops when
//it doesnt see a flag
//reads flag and argument   value for the flag which is +1(assuming always int value right after)
//strcmp to confirm if actual arguement matches with the name of arguements
    int i;
    for (i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-cl") == 0) {//check n parse cl flag for clocktime
            if (i + 1 < argc) {
                max_clock = atoi(argv[++i]);
            } else {//if no int value after
                fprintf(stderr, "Error: missing argument for -cl\n");
                exit(1);
            }
        }

        else if(strcmp(argv[i],"-mem") == 0){//parse memory
            if(i + 1 < argc){
                max_mem = atol(argv[++i]);
            } else {//if no int value after
                fprintf(stderr, "Error: missing argument for -mem\n");
                exit(1);
            }
        }
        //reset
        else {
            cmd_child = &argv[i];
            break;
        }
    }

    if (cmd_child == NULL) {//program for child process to run isnt made
        fprintf(stderr, "Error: No program for child to run.\n");

        exit(1);
    }
    
    

    printf("Parent process: User selected program '%s'\n", cmd_child[0]);
    
    if(max_clock >0){
      printf("Parent prcoess: user set max time to %d\n", max_clock);
      }
    else{
      printf("Next time, you can use the -cl [value] flag to set cpu time limit");
    }
    if(max_mem > 0){
        printf("Parent process: memory limit set to %ld KB\n", max_mem);
    }
    else{
      printf("Next time you can set memory limit with flag -mem [value]");
      }
  
    printf("\n");
    printf("\n");

 
    pid_t pid = fork();
    if (pid < 0) {
        perror("Error forking child process");
        exit(1);
    }

    if (pid == 0) { //child Process
        //kernel force
        if(max_cpu > 0){
          struct rlimit rl;
          rl.rlim_cur = max_cpu; //override, new limit
          rl.rlim_max = max_cpu;
         
          if(setrlimit(RLIMIT_CPU, &rl) == -1){//returns 0 on success. oinforms
            printf("Error in child. Setting cpu limit failed");
            }
            else{
              printf("Child Process: cpu limit set to %d seconds.", max_cpu);
            }
        }
       
        //child process execute program from command line ***AFTER*** initial commands
        execvp(cmd_child[0], cmd_child);
       
        // If execvp returns, it failed
        perror("Error with child (execvp failed)");
        exit(127);
    }


    time_t start_time = time(NULL);
    long max_mem_used = 0; // Tracks the mem usageseen so far

    //fixed
    struct rusage usage;
    memset(&usage, 0, sizeof(usage));
   
    printf("Parent process: moinotoring child with id PID: %d....\n", pid);
    int in_progress = 1;
    while (in_progress) {
        int status, ret; //lab 10 slides
       
        //cant use waitpid. waitpid only returns status and signal that terminated process
        //wait4 used directly deals with user run time and system runtime on return
        //wait 4 returns 0 if child is still in process
        ret = wait4(pid, &status, WNOHANG, &usage); //Wnohang important to not pause program

        if (ret == -1) {
            if (errno == EINTR) {
                // If wait4 was interrupted by SIGTSTP (Ctrl+Z), simply continue the loop.
                continue;
            } else {
                perror("wait4 error");
                //one last check
                kill(pid, SIGKILL);
                exit(1);
            }
        }
       
        if (ret > 0) { // Child finished
              
               
              //gather info before exit 
              if(usage.ru_maxrss > max_mem_used){
                max_mem_used = usage.ru_maxrss;
              }

              if (WIFEXITED(status)) {//if status value is no signal, and uses exit or return
                printf("Back to parent- child finished process normally with status %d\n", WEXITSTATUS(status));
              } else if (WIFSIGNALED(status)) {//if status shows child process terminated prematurely(kill,sigkill)
                printf("Parent process: Child terminated by signal %d\n", WTERMSIG(status));
              }
            //break;
            in_progress = 0;
        }


        if (stop) {
            // crtl+z
            // pause
            pause(); // Wait for SIGCONT or SIGINT
            continue; // Resume monitoring loop after pause() returns (likely due to SIGCONT)
        }

        long current_rss = get_current_rss_kb(pid);


        if(current_rss > max_mem_used){
            max_mem_used = current_rss;
        }
        if(usage.ru_maxrss > max_mem_used){ //just in case. usage = max kb
            max_mem_used = usage.ru_maxrss;
        }


        long cpu_runtime = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;

        // cpu time
        if (max_cpu && cpu_runtime > max_cpu) {
            printf("Parent Process: CPU time reached limit.Terminate child w/ kill signal.\n");
            kill(pid, SIGKILL);
            //break;
            in_progress = 0;
        }
       
        // mem limit check
        if(max_mem > 0 && max_mem_used > max_mem)
        {
          printf("Parent Process: Memory usage reach limit.Terminate child w/ kill signal.\n");
          kill(pid, SIGKILL);
          //break;
          in_progress = 0;
        }
       
        // wall clock time limit reached
        if (max_clock && (time(NULL) - start_time) > max_clock) {
            printf("Parent Process: wall clock time reached limit! terminating child.\n");
            kill(pid, SIGKILL);
            //break;
            in_progress =0;
        }
       
        //for busy work
        usleep(100000); // Sleep 0.1s
    }


    printf("\n Execution Finished \n");
    printf("\n");
    printf("\n");
    printf("Summary statistics:\n");
    printf("Max memory (Peak RSS): %ld KB\n", max_mem_used);
    // printf("User time: %ld sec\n", usage.ru_utime.tv_sec);
    // printf("System time: %ld sec\n", usage.ru_stime.tv_sec);
    return 0;
}
