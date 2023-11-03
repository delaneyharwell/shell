
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#define MaxLine 80
#define MaxArgc 80
#define MaxJob 5

typedef struct{
    pid_t pid;
    char name[MaxLine];
    char status[MaxLine];
} Process;

Process procs[MaxJob];
int job_count = 0;

pid_t child_pid;
pid_t parent_pid;
bool bg;


void remove_newline_ch(char *line)
{
    int new_line = strlen(line) -1;
    if (line[new_line] == '\n')
        line[new_line] = '\0';
}

int parseline(char * cmdline, char * argv[]){
    char * tok  = strtok(cmdline, " ");
    int count = 0;
    while (tok){
        argv[count] = tok;
        count++;
        tok = strtok(NULL, " ");
    }
    argv[count] = NULL;
    return count;
}


bool check_background(char * argv[], int length){
    if (strcmp(argv[length-1], "&") == 0){
        argv[length-1] = NULL;
        return true;
    }
    return false;
}

void remove_from_proc(pid_t pid){
    for (int i = 0; i < job_count; i++){
        if (procs[i].pid == pid){
            for (int k = i; k <= job_count-1; k++)
                procs[k] = procs[k+1];
        }
        break;
    }
    job_count--;
}


void waiting4pid(pid_t processID, Process * P){
    int waitCondition = WUNTRACED | WCONTINUED;
    int currentState;
    pid_t childpid;
    childpid = waitpid(processID, &currentState, waitCondition);
    if (WIFSIGNALED(currentState)){
        printf("\n currentState = Child Exited!\n");
        strcpy(P->status, "Terminated");
        
    }else if (WIFSTOPPED(currentState)){
        printf("\n currentState = Child Stopped!\n");
        strcpy(P->status, "Stopped");
       
    }else{
        strcpy(P->status, "Terminated");
        remove_from_proc(P->pid);
    }
    return;
}


void sigint_handler(int signum){
}

void sigtstp_handler(int signum){
    kill(child_pid, SIGTSTP);
}

void sighandler_childdie(int signum){}

void sigchld_handler(int signum){
    if (bg){
        for (int i = 0; i < job_count; ++i){
            Process  p = procs[i];
            if (procs[i].pid == child_pid){
                strcpy(p.status, "Terminated");
                remove_from_proc(p.pid);
            }
        }
        
    }  
}


Process * findprocess(char * arg, int count){
    
    if (arg[0] == '%'){
        int val = (int)arg[1] - '1';
        return &procs[val];
    }
    pid_t current  = atoi(arg);
    for (int i = 0; i < count; ++i){
        Process  p = procs[i];
        if (procs[i].pid == current){
            return &procs[i];
        }
    }
    return NULL;
}


void print_jobs(){
    for (int i = 0; i < job_count; ++i){
        Process * p = &procs[i];
        if (strcmp(p->status, "Running") == 0)
            printf("[%d] (%d) Running %s\n", i+1, p->pid, p->name);
        else if (strcmp(p->status, "Background/Running") == 0)
            printf("[%d] (%d) Running %s &\n", i+1, p->pid, p->name);
        else if (strcmp(p->status, "Stopped") == 0){
            printf("[%d] (%d) Stopped %s\n", i+1, p->pid, p->name);
        }
    }
}


void remove_element(char * argv[], int index, int *length) {
    for(int i = index; i < *length - 1; i++) {
        argv[i] = argv[i+1];
    }
    argv[*length - 1] = NULL;
    (*length)--;
}


bool check_redirect(char * argv[], char  infile[], char  outfile[], char  appendfile[], int * length){
    int i = 0;
    bool changed = false;
    while(i < *length){
        if (strcmp(argv[i], ">") == 0){
            strcpy(outfile, argv[i+1]);
            remove_element(argv, i, length);
            remove_element(argv, i, length);
            changed = true;
        }else if (strcmp(argv[i], "<") == 0){
            strcpy(infile, argv[i+1]);
            remove_element(argv, i, length);
            remove_element(argv, i, length);
            changed = true;
        }else if (strcmp(argv[i], ">>") == 0){
            strcpy(appendfile, argv[i+1]);
            remove_element(argv, i, length);
            remove_element(argv, i, length);
            changed = true;
        }else{
            ++i;
        }
    }
    return changed;
   
}

void change_redir(char * infile, char * outfile, char * appendfile, mode_t mode){
    if (infile[0] != '\0'){
        int file = open(infile, O_RDONLY, mode);
        if (file < 0){
            perror("Failed to open input file.\n");
            exit(1);
        }
        close(0);
        dup2(file, STDIN_FILENO);
        close(file);
    }
    if (outfile[0] != '\0'){
        int file = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, mode);
        if (file < 0){
            perror("Failed to open output file.\n");
            exit(1);
        }
        close(1);
        dup2(file, STDOUT_FILENO);
        close(file);
        
    }else  if (appendfile[0] != '\0'){
        int file = open(appendfile, O_WRONLY  | O_CREAT | O_APPEND, mode);
        if (file < 0){
            perror("Failed to open append file.\n");
            exit(1);
        }
        close(1);
        dup2(file, STDOUT_FILENO);
        close(file);
        
    }
}


int main(){
    char str[MaxLine];
    mode_t mode = S_IRWXU |S_IRWXG | S_IRWXO;
    int count = 0;
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGCHLD, sigchld_handler);
    parent_pid = getpid();
    
    while (1){
        
        printf("prompt > ");
        
        char * argv[MaxArgc];
        fgets(str, sizeof(str), stdin);
        if (job_count >= 5){
            printf("Cannot have more than 5 active jobs. Please wait for one to finish .\n");
            while (job_count >=5);
        }
        remove_newline_ch(str);
        char copy[MaxArgc];
        strcpy(copy, str);
        char  infile[128] = {'\0'};
        char  outfile[128] = {'\0'};
        char appendfile[128] = {'\0'};
        int value_num = parseline(copy, argv);
        bg = check_background(argv, value_num);
        if (bg){
            argv[--value_num] = '\0';
            
        }
        if (strcmp(argv[0], "cd") == 0){
            chdir(argv[value_num-1]);
        }else if (strcmp(argv[0], "quit") == 0){
            for (int i = 0 ; i < job_count; ++i){
                Process * p = &procs[i];
                if (strcmp(p->status, "Stopped") != 0){
                    kill(p->pid, SIGTERM);
                }
            }
            while (wait(NULL) != -1);
            break;
        }else if (strcmp(argv[0], "jobs") == 0){
            print_jobs();
        }else if (strcmp(argv[0], "fg") == 0){
            Process * p = findprocess(argv[1], job_count);
            int status;
            if (p != NULL){
                if (strcmp(p->status, "Background/Running") == 0){
                    strcpy(p->status, "Running");
                    waitpid(p->pid, &status, WUNTRACED);
                }else if (strcmp(p->status, "Stopped") == 0){
                    strcpy(p->status, "Running");
                    child_pid = p->pid;
                    kill( p->pid, SIGCONT);
                    waitpid(p->pid, &status, WUNTRACED);  
                }
                if (WIFSTOPPED(status)){
                        printf("\n currentState = Child Stopped!\n");
                        strcpy(p->status, "Stopped");
                }else{
                    if (WIFSIGNALED(status)){
                        printf("\n currentState = Child Exited!\n");
                        strcpy(p->status, "Terminated");
                    }
                    remove_from_proc(p->pid); 
                }
            }
        }else if (strcmp(argv[0], "bg") == 0){
            Process * p = findprocess(argv[1],  job_count);
            if (p != NULL)
                if (strcmp(p->status, "Stopped") == 0){
                    kill( p->pid, SIGCONT);
                    strcpy(p->status, "Background/Running");
                }
        }else if (strcmp(argv[0], "kill") == 0){
            Process * p = findprocess(argv[1], job_count);
            if (p != NULL){
                kill( p->pid, SIGINT);
                remove_from_proc(p->pid);
            }
        }else{
            pid_t pid;
            pid = fork();
            child_pid = pid;
            if (pid == 0){
                if (check_redirect(argv, infile, outfile, appendfile, &value_num))
                    change_redir(infile, outfile, appendfile, mode);
                if (execvp(argv[0], argv) < 0)
                {
                    if (execv(argv[0], argv) < 0)
                    {
                        printf("Error executing the file.\n");
                        exit(0);
                    }
                }
            }
            else{
                if (!bg ){
                    procs[job_count].pid = child_pid;
                    strcpy(procs[job_count].name, argv[0]);
                    strcpy(procs[job_count++].status, "Running");
                    waiting4pid(child_pid, &procs[job_count-1]);
                }else{
                    procs[job_count].pid = child_pid;
                    strcpy(procs[job_count].name, argv[0]);
                    strcpy(procs[job_count++].status, "Background/Running");
                }
            }
        }
    }
}
