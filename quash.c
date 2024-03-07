#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>


int found_pipe= 0;
int pipe_location[15];
int found_hashtag= 0;
int hashtag_location[15];
int found_input_redirect= 0;
int input_redirect_location[15];
int found_output_redirect= 0;
int output_redirect_location[15];
int found_append= 0;
int append_location[15];
pid_t job_manager_pid;
int num_background_jobs = 0; //num of active bacgkround jobs
pthread_mutex_t mutex;
pthread_cond_t cond;
int is_job_manager_ready = 0;
pthread_t job_manager_thread;
int job_increment = 1;

struct BackgroundJob{ //to store the background jobs but limiting them to a character count of 64
    int job_id;
    pid_t pid;
    char command[64];
    int status;
};

struct BackgroundJob background_jobs[32];


void add_background_job(pid_t pid, const char *command){ // gonna be honest looks this up because had no idea.
    struct BackgroundJob *job = &background_jobs[num_background_jobs];
    job->job_id = num_background_jobs +1;
    job->pid = pid;
    background_jobs[num_background_jobs].status = 0;
    strncpy(job->command, command, sizeof(job->command)-1);
    job->command[sizeof(job->command)-1] = '\0';
    printf("Background job [%d] %d has started %s\n", num_background_jobs+1, pid, command);
}


void reorder_jobs(int removedJobID) {
    background_jobs[removedJobID].status = 0;
    for (int i = 0; i < num_background_jobs; i++) {
        if (background_jobs[i].job_id == removedJobID) {
            for (int j = i; j < num_background_jobs - 1; j++) {
                background_jobs[j] = background_jobs[j + 1];

            }
            num_background_jobs--;
            break;
        }
    }
}
void list_background_jobs(){
    for(int i= 0; i < num_background_jobs; i++){
        printf("[%d] %d %s\n",background_jobs[i].job_id, background_jobs[i].pid, background_jobs[i].command);
    }
}

void remove_quotes(char *str){
    int length = strlen(str);
    char single='\'';
    if (length >= 2 && str[0] == '"' && str[length-1]=='"'){
        for(int i=0; i<length-1; i++){
            str[i] = str[i+1];
        }
        str[length-2] = '\0';
    }
    else if(length >= 2 && str[0] == single && str[length-1]==single){
        for(int i=0; i<length-1; i++){
            str[i] = str[i+1];
        }
        str[length-2] = '\0';
    }
}


bool containsAmpersand(char *line) {
    while (*line) {
        if (*line == '&') {
            return true; // Found the '&' symbol
        }
        line++; // Move to the next character
    }
    return false; // '&' symbol not found
}


void removeAmpersand(char *input) {
    char *ampersand_ptr = strchr(input, '&');
    if (ampersand_ptr != NULL) {
        *ampersand_ptr = ' '; // Replace '&' with a space to effectively remove it
    }
}


void built_ins(char *args[], int hastag_location[]){

    if(hastag_location[0]!=-1){             //takes care of hashtags in any location
        for (int i=hastag_location[0]; i < 64; i++){
            args[i] = NULL;
        }
    }

    if(strcmp(args[0], "exit") == 0){
        exit(1);
    }

    else if (strcmp(args[0], "quit") == 0){
        exit(1);
    }

    else if(strcmp(args[0], "echo") == 0){
        int i=0;
        while(args[i]!=NULL){
            remove_quotes(args[i]);
            i++;
        }
        if(args[1]==NULL){
            execvp(args[0], args);
        }
        char temp[64];
        strcpy(temp, args[1]);
        if (temp[0] == '$'){
            char *modified_temp = temp+1;
            printf("%s\n", getenv(modified_temp));
        }
        else{
        pid_t pid = fork();
        if (pid==0){
            execvp("/usr/bin/echo", args);
            }  
        else{
            wait(NULL);
        } // When running the command "echo hi #comment" the output is hi #comment
        }
    }

    else if(strcmp(args[0], "pwd") == 0){
        char cur_dir[256];
        getcwd(cur_dir, sizeof(cur_dir));
        printf(cur_dir, "%s");
        printf("\n");

    }
    else if(strcmp(args[0], "kill") == 0){
        if(args[1]==NULL)
            printf("Provide job ID");
    }

    else if(strcmp(args[0], "jobs") == 0){
        list_background_jobs();
    }

    else if(strcmp(args[0], "ls") == 0){
        pid_t pid = fork();
        if (pid==0){
            execvp("/usr/bin/ls", args);
            }  
        else{
            wait(NULL);
    }
    }

    else if(strcmp(args[0], "cd") == 0){
        if (args[1] == NULL){
            chdir(getenv("HOME"));
        }
        else{chdir(args[1]);}
    }

    else if(strcmp(args[0], "export") == 0){
        char env_var[64] = "";
        char temp[64];
        strcpy(temp, args[1]);
        int equal_index=0;
        for(int i=0; i < strlen(args[1]); i++){
            if (temp[i] == '='){
                equal_index=i;
                break;
            }
        }
        strncpy(env_var, args[1], equal_index); //env_var has the name of the variable
        args[1] += (equal_index+1); //args now contains the value of the env var (not set yet)
        char tem[64];
        strcpy(tem, args[1]);
        if (tem[0] == '$'){
            char *modified_tem = tem+1;
            setenv(env_var, getenv(modified_tem), 1);}
        else {
            setenv(env_var, args[1], 1);}
    }
    else{;
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execvp(args[0], args);
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Fork failed");
        } else {
            // Parent process
            wait(NULL);
        }      
    }
}

void background_built_ins(char *args[], int hastag_location[]){

    if(hastag_location[0]!=-1){             //takes care of hashtags in any location
        for (int i=hastag_location[0]; i < 64; i++){
            args[i] = NULL;
        }
    }

    if(strcmp(args[0], "exit") == 0){
        exit(1);
    }

    else if (strcmp(args[0], "quit") == 0){
        exit(1);
    }

    else if(strcmp(args[0], "echo") == 0){
        int i=0;
        while(args[i]!=NULL){
            remove_quotes(args[i]);
            i++;
        }
        char temp[64];
        strcpy(temp, args[1]);
        if (temp[0] == '$'){
            char *modified_temp = temp+1;
            printf("%s\n", getenv(modified_temp));
        }
        else{
        pid_t pid = fork();
        if (pid==0){
            execvp("/usr/bin/echo", args);
            }  
        else{
            wait(NULL);
        } // When running the command "echo hi #comment" the output is hi #comment
        }
    }

    else if(strcmp(args[0], "pwd") == 0){
        char cur_dir[256];
        getcwd(cur_dir, sizeof(cur_dir));
        printf(cur_dir, "%s");
        printf("\n");

    }
    else if(strcmp(args[0], "kill") == 0){
        if(args[1]==NULL)
            printf("Provide job ID");
    }

    else if(strcmp(args[0], "jobs") == 0){
        list_background_jobs();
    }

    else if(strcmp(args[0], "ls") == 0){
        pid_t pid = fork();
        if (pid==0){
            execvp("/usr/bin/ls", args);
            }  
        else{
            wait(NULL);
    }
    }

    else if(strcmp(args[0], "cd") == 0){
        if (args[1] == NULL){
            chdir(getenv("HOME"));
        }
        else{chdir(args[1]);}
    }

    else if(strcmp(args[0], "export") == 0){
        char env_var[64];
        char temp[64];
        strcpy(temp, args[1]);
        int equal_index=0;
        for(int i=0; i < strlen(args[1]); i++){
            if (temp[i] == '='){
                equal_index=i;
                break;
            }
        }
        strncpy(env_var, args[1], equal_index); //env_var has the name of the variable
        args[1] += (equal_index+1); //args now contains the value of the env var (not set yet)
        char tem[64];
        strcpy(tem, args[1]);
        if (tem[0] == '$'){
            char *modified_tem = tem+1;
            setenv(env_var, getenv(modified_tem), 1);}
        else {
            setenv(env_var, args[1], 1);}
        printf("%s\n", getenv(env_var));
    }
    else{;
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execvp(args[0], args);
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Fork failed");
        } else {
            // Parent process
            wait(NULL);
        }      
    }
}  

void parser(char *input, char *args[], int *found_pipe, int pipe_location[], int *found_hashtag, int hashtag_location[], int *found_input_redirect, int input_redirect_location[], int *found_output_redirect, int output_redirect_location[], int *found_append, int append_location[], int *background_flag){ //for pipes make it so it reads up to a pipe and saves everything as one arguments and everything after as another
    int i=0;
    int j=0;
    int k=0;
    int l=0;
    int m=0;
    int n=0;
   
    if (containsAmpersand(input)) {
        removeAmpersand(input);
        int pid = getpid();
        add_background_job(pid + job_increment, input);
        num_background_jobs++;
        *background_flag = *background_flag + 1;
        job_increment++;
    }
        args[i] = strtok(input, " \t\n");
    while (args[i] != NULL){
        if ((strcmp(args[i], "#") == 0 ) || (args[i][0] == '#')){
            *found_hashtag = *found_hashtag +1;
            hashtag_location[j] = i;
            j++;
        }
        if (strcmp(args[i], "|") == 0){
            *found_pipe = *found_pipe +1;
            pipe_location[k] = i;
        }
        if (strcmp(args[i], "<") == 0){
            *found_input_redirect = *found_input_redirect +1;
            input_redirect_location[l] = i;
            l++;
        }
        if(strcmp(args[i], ">") == 0){
            *found_output_redirect = *found_output_redirect +1;
            output_redirect_location[m] = i;
            m++;
        }
        if (strcmp(args[i], ">>") == 0){
            *found_append = *found_append +1;
            append_location[n] = i;
            n++;
        }

        i++;
        args[i] = strtok(NULL, " \t\n");
    }
}

void background_parser(char *input, char *args[], int *found_pipe, int pipe_location[], int *found_hashtag, int hashtag_location[], int *found_input_redirect, int input_redirect_location[], int *found_output_redirect, int output_redirect_location[], int *found_append, int append_location[]){ //for pipes make it so it reads up to a pipe and saves everything as one arguments and everything after as another
    int i=0;
    int j=0;
    int k=0;
    int l=0;
    int m=0;
    int n=0;
   

    args[i] = strtok(input, " \t\n");
    while (args[i] != NULL){
        if ((strcmp(args[i], "#") == 0 ) || (args[i][0] == '#')){
            *found_hashtag = *found_hashtag +1;
            hashtag_location[j] = i;
            j++;
        }
        if (strcmp(args[i], "|") == 0){
            *found_pipe = *found_pipe +1;
            pipe_location[0] = i;
           
        }
        if (strcmp(args[i], "<") == 0){
            *found_input_redirect = *found_input_redirect +1;
            input_redirect_location[l] = i;
            l++;
        }
        if(strcmp(args[i], ">") == 0){
            *found_output_redirect = *found_output_redirect +1;
            output_redirect_location[m] = i;
            m++;
        }
        if (strcmp(args[i], ">>") == 0){
            *found_append = *found_append +1;
            append_location[n] = i;
            n++;
        }

        i++;
        args[i] = strtok(NULL, " \t\n");
    }
}

void run_background_jobs() {
    if (num_background_jobs > 0) {
        for (int i = 0; i < num_background_jobs; i++) {
            pid_t pid = background_jobs[i].pid;
            char *args[64];
            if (num_background_jobs != 0) {
                if (background_jobs[i].status == 0) {
                    background_jobs[i].status = 1;
                    background_parser(background_jobs[i].command, args, &found_pipe, pipe_location, &found_hashtag, hashtag_location, &found_input_redirect, input_redirect_location, &found_output_redirect, output_redirect_location, &found_append, append_location);
                    background_built_ins(args,hashtag_location);
                    break;
                } else if (background_jobs[i].status == 1) {
                    printf("Background job [%d] %d has completed\n", background_jobs[i].job_id, pid);
                    reorder_jobs(background_jobs[i].job_id);
                    break;
                }
            } else {
            }
        }

    }
}
void *job_manager(void *arg) {
    is_job_manager_ready = 1;
    pthread_cond_signal(&cond);
     while (1) {
            found_pipe= 0;
            pipe_location[15];
            found_hashtag= 0;
            hashtag_location[15];
            hashtag_location[0] = -1;
            found_input_redirect= 0;
            input_redirect_location[15];
            found_output_redirect= 0;
            output_redirect_location[15];
            found_append= 0;
            append_location[15];
            run_background_jobs();
            fflush(stdout);
            sleep(1);
        }
}

void execute_pipe (char *command1[], char *command2[])
{

    int my_pipe[2];
    pipe(my_pipe);
    int pid;
    pid = fork();
    if(pid ==0) {
        dup2(my_pipe[1],STDOUT_FILENO);
            close(my_pipe[0]);
            close(my_pipe[1]);
             printf("1");
        if (execvp(*command1, command1) == -1)
                        perror("Pipe Error");
            exit(1);
    }
    else {
        dup2(my_pipe[0],STDIN_FILENO);
            close(my_pipe[1]);
            close(my_pipe[0]);
            wait(0);

        if (execvp(*command2, command2) == -1)
                        perror("Pipe Error");
    }  
}

void execute_output_redirect(char *command1[], char *file[]){ //CLEAR THE FILE FIRST BEFORE WRITING
    int fd;
    fd = open(file[0], O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if (!fork()){
        close(1);
        dup(fd);
        if (execvp(*command1, command1) == -1){
            perror("I/O Error"); 
            exit(1);
        }
    }
    else{
        close(fd);
        wait(NULL);
    }
}

void execute_append(char *command1[], char *file[]) {
    int fd;
    fd = open(file[0], O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (!fork()) {
        close(1);
        dup(fd);
        if (execvp(*command1, command1) == -1) {
            perror("I/O Error");
            exit(1);
        }
    } else {
        close(fd);
        wait(NULL);
    }
}
void execute_input_redirection(char *command1[], char *file[]){
    int fd;
    fd = open(file[0], O_RDONLY);
    if (!fork()){
        close(0);
        dup(fd);
        if (execvp(*command1, command1) == -1){
            perror("I/O Error");
            exit(1);
        }
    }
    else{
        close(fd);
        wait(NULL);
    }
}



int main(){
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    if (pthread_create(&job_manager_thread, NULL, job_manager, NULL) != 0) {
        perror("Error creating job manager thread");
        exit(1);
    }
    // Wait for the job manager thread to be ready
    pthread_mutex_lock(&mutex);
    while (!is_job_manager_ready) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    char user_input[1024];
    char *args[64];
    while(1){
        found_pipe= 0;
            pipe_location[15];
            found_hashtag= 0;
            hashtag_location[15];
            hashtag_location[0] = -1;
            found_input_redirect= 0;
            input_redirect_location[15];
            found_output_redirect= 0;
            output_redirect_location[15];
            found_append= 0;
            append_location[15];    
            int background_flag = 0;
        printf("[QUASH]$ ");
        if(fgets(user_input, sizeof(user_input), stdin)== NULL){
            break;
        }

        user_input[strlen(user_input)-1] = '\0';
        parser(user_input, args, &found_pipe, pipe_location, &found_hashtag, hashtag_location, &found_input_redirect, input_redirect_location, &found_output_redirect, output_redirect_location, &found_append, append_location, &background_flag);

         if(args[0]==NULL){
            continue;
        }

        if (strcmp(args[0], "#") == 0){
            continue;
        }
        if (background_flag == 1){
            continue;
        }
        if(found_pipe == 1){
            int pid = fork();
            if(pid == 0){

            args[pipe_location[0]] = '\0';
            execute_pipe(args,&args[pipe_location[0]+1]);
            exit(1);
            }else{
                wait(NULL);
            }

        }

        else if(found_output_redirect == 1){
            args[output_redirect_location[0]] = '\0';
            execute_output_redirect(args, &args[output_redirect_location[0]+1]);
        }
        else if(found_append == 1){
            args[append_location[0]] = '\0';
            execute_append(args, &args[append_location[0]+1]);
        }
        else if(found_input_redirect == 1){
            args[input_redirect_location[0]] = '\0';
            execute_input_redirection(args, &args[input_redirect_location[0]+1]);
        }

        else if(background_flag == 0){
            built_ins(args, hashtag_location);
        }
    }
    return 0;
}

