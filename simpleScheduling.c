#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/msg.h> 

#define CHILDNUM 10
#define QUANTUM 80
#define TICK 10
#define KEY 2020
#define MAX_PID 32768

typedef struct _process{
    int pid; //for mtype
    int CPUburst;
    int IOburst; //after
    int arrival; //ms
    int remainCPU;
    int remainIO;
    int wait; //wating time in ready queue
    int response; //first response - arrival
} process;

typedef struct _simulData{
    int watingTime;
    int turnaroundTime; //completion - around
    int responseTime;
    int contextSwitch;
    int count; //for average
} simulData;

typedef struct _node{
    process data;
    struct _node* left;
    struct _node* right; 
} node;

typedef struct _queue{ // de <---------------- en
    node* front; //de left
    node* rear;  //en right
    int length;
} queue;

typedef struct _message{
    long msg_type;
    process my_process;
} message;


void timer_handler(int signum);
void init_process(process* my_p);
void init_queue(queue* my_q);
void enqueue(queue* my_q, process data);
process dequeue(queue* my_q);
void clear_queue(queue* my_q);
void print_rqueue(queue* my_q); //debug
void print_wqueue(queue* my_q); //debug
process end_io(queue* my_q, node* out);

/*global variables for signal handler*/
queue* readyQueuePtr;
queue* waitingQueuePtr;
simulData* dataPtr;
int tick_counter;
int quantum_counter;
long long start;

int main(){
    int queue_id = msgget(KEY, IPC_CREAT|0666);
    struct timeval time;
    gettimeofday(&time, NULL);
    start = time.tv_sec * 1000 + time.tv_usec / 1000;

    pid_t pid, parent = getpid();
    for (int i = 0; i < CHILDNUM; i++)
        if (getpid() == parent) //dont fork grandchild
            pid = fork();

    if (pid < 0){
        fprintf(stderr, "failed");
        return -1;
    }

    else if (pid == 0){ //child process
        
        process my_p;
        init_process(&my_p);
        message msg;
        msg.msg_type = getpid();
        msg.my_process = my_p;

        if (msgsnd(queue_id, &msg, sizeof(process), IPC_NOWAIT) == -1){
            printf("failed to send %d\n", getpid());
            exit(1);
        }
        else
            printf("send complete\n");

        
        while (1){
            //wait for msg

            if (msgrcv(queue_id, &msg, sizeof(process), getpid() + MAX_PID, 0) == -1){
                printf("failed to receive %d\n", getpid());
                exit(1);
            }
            my_p = msg.my_process;
            if (my_p.remainCPU <= 0 && my_p.remainIO <= 0){
                init_process(&my_p);
                msg.my_process = my_p;
                msg.msg_type = my_p.pid;
                if (msgsnd(queue_id, &msg, sizeof(process), 0) == -1){
                    printf("failed to send\n");
                    exit(1);
                }
            }
        }
    }

    
    else{ //parent process
        queue ready_queue;
        queue waiting_queue;
        simulData simulation;
        memset(&simulation, 0, sizeof(simulData));
        init_queue(&ready_queue);
        init_queue(&waiting_queue);
        readyQueuePtr = &ready_queue;
        waitingQueuePtr = &waiting_queue;
        dataPtr = &simulation;

        struct sigaction sa;
        struct itimerval timer;

        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = &timer_handler;
        sigaction (SIGVTALRM, &sa, NULL);

        timer.it_value.tv_sec = 0;
        timer.it_value.tv_usec = TICK * 1000;

        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = TICK * 1000;

        setitimer (ITIMER_VIRTUAL, &timer, NULL);

        while (1);
    }

    return 0;
}

void timer_handler(int signum){
    int queue_id = msgget(KEY, IPC_CREAT|0666);
    message msg;
    struct timeval tm;

    if (tick_counter > TICK * 1000){ //end condition

        while (msgrcv(queue_id, &msg, sizeof(process), -MAX_PID, IPC_NOWAIT) > 0){
            enqueue(readyQueuePtr, msg.my_process);
        }

        printf("==========END=========\n");
        printf("process count: %d\n", dataPtr->count);
        printf("Average Waiting Time: %d\n", dataPtr->watingTime / dataPtr->count);
        printf("Average Turnaround Time: %d\n", dataPtr->turnaroundTime / dataPtr->count);
        printf("Average Response Time: %d\n", dataPtr->responseTime / dataPtr->count);
        printf("Context Switch: %d times\n", dataPtr->contextSwitch);

        node* killer = readyQueuePtr->front;
        while (killer != NULL){
            kill(killer->data.pid, SIGKILL);
            killer = killer->right;
        }

        killer = waitingQueuePtr->front;
        while (killer != NULL){
            kill(killer->data.pid, SIGKILL);
            killer = killer->right;
        }

        clear_queue(readyQueuePtr);
        clear_queue(waitingQueuePtr);

        msgctl(queue_id, IPC_RMID, NULL);
    
        exit(1);
    }



    while (msgrcv(queue_id, &msg, sizeof(process), -MAX_PID, IPC_NOWAIT) > 0){
        gettimeofday(&tm, NULL);
        long long now = tm.tv_sec * 1000 + tm.tv_usec / 1000;
        msg.my_process.arrival = now - start;
        enqueue(readyQueuePtr, msg.my_process);
        printf("%d ENQUEUE SUCCESS\n", msg.my_process.pid);
    }

    //wait


    if (readyQueuePtr->length != 0){

        node* tmp = readyQueuePtr->front->right;
        while (tmp != NULL){
            tmp->data.wait += TICK;
            tmp = tmp->right;
        }

        readyQueuePtr->front->data.remainCPU -= TICK;

        if (readyQueuePtr->front->data.response == -1){ //response
            gettimeofday(&tm, NULL);
            long long now = tm.tv_sec * 1000 + tm.tv_usec / 1000;
            int passed = now - start;
            readyQueuePtr->front->data.response = passed - readyQueuePtr->front->data.arrival;
        }
        
        msg.msg_type = readyQueuePtr->front->data.pid + MAX_PID;

        if (readyQueuePtr->front->data.remainCPU <= 0 || quantum_counter == QUANTUM){
            (dataPtr->contextSwitch)++;
            quantum_counter = 0;
            srand(tm.tv_usec);
            if (rand()%2 == 0){ // 1/2 IO
                readyQueuePtr->front->data.remainIO += (rand()%TICK + 1) * 30; //max io burst = 300
                readyQueuePtr->front->data.IOburst += readyQueuePtr->front->data.remainIO;
                printf("IO CALL for %d\n", readyQueuePtr->front->data.pid);
                msg.my_process = readyQueuePtr->front->data;
                enqueue(waitingQueuePtr, dequeue(readyQueuePtr));
            }
            else{
                msg.my_process = readyQueuePtr->front->data;
                if (readyQueuePtr->front->data.remainCPU <= 0){
                    printf("CPU BURST END, NO IO BURST. %d DEQUEUE.\n", readyQueuePtr->front->data.pid);
                    gettimeofday(&tm, NULL);
                    long long now = tm.tv_sec * 1000 + tm.tv_usec / 1000;
                    int passed = now - start;
                    int trn = passed - readyQueuePtr->front->data.arrival;
                    dataPtr->turnaroundTime += trn;
                    dataPtr->responseTime += readyQueuePtr->front->data.response;
                    dataPtr->watingTime += readyQueuePtr->front->data.wait;
                    (dataPtr->count)++;
                    dequeue(readyQueuePtr);
                }
                else{//>0
                    printf("QUANTUM END. %d GOTO REAR.\n", readyQueuePtr->front->data.pid);
                    enqueue(readyQueuePtr, dequeue(readyQueuePtr));
                }
            }
        }

        if (msgsnd(queue_id, &msg, sizeof(process), 0) == -1){
            printf("failed to send\n");
            exit(1);
        }    

    }

    node* wtmp = waitingQueuePtr->front;
    while (wtmp != NULL){
        wtmp->data.remainIO -= TICK;
        msg.msg_type = wtmp->data.pid + MAX_PID;
        msg.my_process = wtmp->data;
        if (msgsnd(queue_id, &msg, sizeof(process), 0) == -1){
            printf("failed to send\n");
            exit(1);
        }
        if (wtmp->data.remainIO <= 0 && wtmp->data.remainCPU <= 0){
            printf("%d IO BURST and CPU BURST END.\n", wtmp->data.pid);
            gettimeofday(&tm, NULL);
            long long now = tm.tv_sec * 1000 + tm.tv_usec / 1000;
            int passed = now - start;
            int trn = passed - wtmp->data.arrival;
            dataPtr->turnaroundTime += trn;
            dataPtr->responseTime += wtmp->data.response;
            dataPtr->watingTime += wtmp->data.wait;
            (dataPtr->count)++;
            end_io(waitingQueuePtr, wtmp);
        }
        else if (wtmp->data.remainIO <= 0){
            printf("IO BURST END. %d GOTO Ready Queue\n", wtmp->data.pid);
            enqueue(readyQueuePtr, end_io(waitingQueuePtr, wtmp));
        }
        wtmp = wtmp->right;
    }

    //debug
    printf("==========TICK: %4d==========\n", tick_counter / TICK);
    print_rqueue(readyQueuePtr);
    print_wqueue(waitingQueuePtr);

    tick_counter += TICK;
    quantum_counter += TICK;
}

void init_process(process* my_p){
    struct timeval time;
    gettimeofday(&time, NULL);
    srand(time.tv_usec);
    my_p->CPUburst = (rand()%TICK + 1) * 20; //max burst = 200
    my_p->pid = getpid();
    my_p->IOburst = 0;
    my_p->arrival = 0;
    my_p->remainCPU = my_p->CPUburst;
    my_p->remainIO = my_p->IOburst;
    my_p->wait = 0;
    my_p->response = -1; //if -1 -> should update response time
}

void init_queue(queue* my_q){
    my_q->front = NULL;
    my_q->rear = NULL;
    my_q->length = 0;
}

void enqueue(queue* my_q, process data){
    node* newnode = (node*)malloc(sizeof(node));
    newnode->data = data;
    if (my_q->length == 0){
        newnode->left = NULL;
        newnode->right = NULL;
        my_q->front = newnode;
        my_q->rear = newnode;
    }

    else{
        newnode->left = my_q->rear;
        newnode->right = NULL;
        my_q->rear->right = newnode;
        my_q->rear = newnode;
    }

    (my_q->length)++;
}

process dequeue(queue* my_q){
    if (my_q->length == 0)
        exit(1);

    else if (my_q->length == 1){
        process rdata = my_q->front->data;
        free(my_q->front);
        init_queue(my_q);
        return rdata;
    }

    else{
        node* tmp = my_q->front;
        process rdata = tmp->data;
        my_q->front = tmp->right;
        free(tmp);
        my_q->front->left = NULL;
        (my_q->length)--;
        return rdata;
    }

}

void clear_queue(queue* my_q){
    while (my_q->length != 0)
        dequeue(my_q);
}

void print_rqueue(queue* my_q){
    if (my_q->length == 0)
        return;
    node* tmp = my_q->front;
    int cnt = 0;
    printf("----------READY QUEUE----------\n");
    while (tmp != NULL){
        printf("pid: %4d rmCPUburst: %3d rmIOburst: %3d arrival: %5d\n", 
        tmp->data.pid, tmp->data.remainCPU, tmp->data.remainIO, tmp->data.arrival);
        tmp = tmp->right;
        cnt++;
    } 
    printf("count: %d\n", cnt);
    return;
}


void print_wqueue(queue* my_q){
    if (my_q->length == 0)
        return;
    node* tmp = my_q->front;
    int cnt = 0;
    printf("----------WAITING QUEUE----------\n");
    while (tmp != NULL){
        printf("pid: %4d rmCPUburst: %3d rmIOburst: %3d arrival: %5d\n", 
        tmp->data.pid, tmp->data.remainCPU, tmp->data.remainIO, tmp->data.arrival);
        tmp = tmp->right;
        cnt++;
    } 
    printf("count: %d\n", cnt);
    return;
}


process end_io(queue* my_q, node* out){
    if (out == NULL)
        exit(1);
    process rdata = out->data;
    if (out->left == NULL && out->right == NULL){
        free(out);
        init_queue(my_q);
        return rdata;
    }
    else if (out->left == NULL){ //front
        my_q->front = out->right;
        my_q->front->left = NULL;
        (my_q->length)--;
        free(out);
        return rdata;
    }
    else if (out->right == NULL){ //rear
        my_q->rear = out->left;
        my_q->rear->right = NULL;
        (my_q->length)--;
        free(out);
        return rdata;
    }
    else{//
        out->left->right = out->right;
        out->right->left = out->left;
        (my_q->length)--;
        free(out);
        return rdata;
    }
}
