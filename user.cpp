#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <string.h>
#include <cstring>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <fstream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <errno.h>
#include <random>
#include <chrono>
using namespace std;

// Constants for simulation behavior
#define TERMINATION_CHANCE 1  // 0.1% chance to terminate every loop
#define READ_CHANCE 85        // 85% chance to read, 15% chance to write
#define DISPATCH_AMOUNT 1e7
#define CHILD_LAUNCH_AMOUNT 1000
#define UNBLOCK_AMOUNT 1000
#define MSGQ_FILE_PATH "msgq.txt"
#define MSGQ_PROJ_ID 65
#define PERMS 0644
#define MSG_GRANTED 4
#define MSG_BLOCKED 3
#define MSG_WRITE 2
#define MSG_READ 1
#define TOTAL_RESOURCES 10
#define TOTAL_INSTANCES 20

// System clock structure
typedef struct SystemClock {
    int seconds;
    int nanosecond;
} SystemClock;

// Process Control Block structure
struct ProcessControlBlock {
    int isOccupied;
    pid_t pid;
    int startSecs;
    int startNanos;
    int blocked;
    int resourcesHeld[TOTAL_RESOURCES];
};

// Message buffer structure
typedef struct MessageBuffer {
        long mtype;
        int msgCode;
        int memoryAddress;
        pid_t sender;
} MessageBuffer;

// Function to increment the system clock
void IncrementClock(SystemClock* c, int increment_amount){
    c->nanosecond = c->nanosecond + increment_amount;
    if (c->nanosecond >= 1e9){
        c->nanosecond -= 1e9;
        c->seconds++;
    }
}

// Function to add time to a given time
void timeAddition(int *addend1Secs, int *addend1Nanos, int addend2Nanos){
    *addend1Nanos += addend2Nanos;
    if (*addend1Nanos >= 1e9){
        *addend1Nanos -= 1e9;
        *addend1Secs++;
    }
}


void InitializeProcessTable(ProcessControlBlock processTable[]){
    for(int i = 0; i < 20; i++){
        processTable[i].isOccupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSecs = 0;
        processTable[i].startNanos = 0;
        processTable[i].blocked = 0;
        for(int j = 0; j < TOTAL_RESOURCES; j++){
            processTable[i].resourcesHeld[j] = 0;
        }
    }
}

int FindEmptyProcessSlot(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (!processTable[i].isOccupied){
            return (i + 1);
        }
    }
    return 0;
}

int CountActiveProcesses(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    int numProcesses = 0;
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied){
            numProcesses++;
        }
    }
    return (numProcesses == 0) ? 1 : numProcesses;
}

bool IsProcessTableEmpty(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied){
            return 0;
        }
    }
    return 1;
}

bool AreAllProcessesBlocked(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied && !processTable[i].blocked){
            return 0;
        }
    }
    return 1;
}

void DisplayProcessTable(ProcessControlBlock processTable[], int maxSimultaneousProcesses, int seconds, int nanosecond, std::ostream& outputFile){
    static int next_print_secs = 0;
    static int next_print_nanos = 0;

    if(seconds > next_print_secs || (seconds == next_print_secs && nanosecond > next_print_nanos)){
        printf("OSS PID: %d  SysClockS: %d  SysClockNano: %d  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n", getpid(), seconds, nanosecond);
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << seconds << "  SysClockNano " << nanosecond << "  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n";
        for(int i = 0; i < maxSimultaneousProcesses; i++){
            std::string tab = (processTable[i].startNanos == 0) ? "\t\t" : "\t";
            std::string r_list = "";

            for(int j = 0; j < TOTAL_RESOURCES; j++){
                r_list += static_cast<char>(65 + j);
                r_list += ":";
                r_list += std::to_string(processTable[i].resourcesHeld[j]);
                r_list += " ";
            }

            std::cout << std::to_string(i + 1) << "\t" << std::to_string(processTable[i].isOccupied) << "\t" << std::to_string(processTable[i].pid) << "\t" << std::to_string(processTable[i].startSecs) << "\t" << std::to_string(processTable[i].startNanos) << tab << std::to_string(processTable[i].blocked) << "\t" << r_list << std::endl;
            outputFile << std::to_string(i + 1) << "\t" << std::to_string(processTable[i].isOccupied) << "\t" << std::to_string(processTable[i].pid) << "\t" << std::to_string(processTable[i].startSecs) << "\t" << std::to_string(processTable[i].startNanos) << tab << std::to_string(processTable[i].blocked) << "\t" << r_list << std::endl;
        }
        next_print_nanos = next_print_nanos + 500000000;
        if (next_print_nanos >= 1000000000){
            next_print_nanos = next_print_nanos - 1000000000;
            next_print_secs++;
        }
    }
}

void RemoveProcessFromTable(ProcessControlBlock processTable[], pid_t pid, int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].pid == pid){
            processTable[i].isOccupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSecs = 0;
            processTable[i].startNanos = 0;
            processTable[i].blocked = 0;
            for(int j = 0; j < TOTAL_RESOURCES; j++){
                processTable[i].resourcesHeld[j] = 0;
            }
            return;
        }
    }
}

void UpdateBlockedProcess(ProcessControlBlock processTable[], pid_t pid, int maxSimultaneousProcesses, int blockedUntilSec, int blockedUntilNano){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].pid == pid){

            processTable[i].blocked = 1;
            return;
        }
    }
}

void TerminateAllProcesses(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].isOccupied){
            kill(processTable[i].pid, SIGKILL);
        }
    }
}

bool IsProcessPresent(ProcessControlBlock processTable[], int maxSimultaneousProcesses, pid_t pid){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].pid == pid){
            return true;
        }
    }
    return false;
}

int GetProcessIndex(ProcessControlBlock processTable[], int maxSimultaneousProcesses, pid_t pid){
    for (int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].pid == pid){
            return i;
        }
    }
    return -1;
}

// Utility function to generate a random number
int GenerateRandomNumber(int min, int max, int pid) {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count() * pid;
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(min, max);
    int random_number = distribution(generator);
    return random_number;
}

// Main function simulating a user process
int main(int argc, char** argv) {
    SystemClock* shm_clock;
        key_t clock_key = ftok("/tmp", 35);
        int shmtid = shmget(clock_key, sizeof(SystemClock), 0666);
        shm_clock = (SystemClock*)shmat(shmtid, NULL, 0);

        int msgqid = 0;
        key_t msgq_key;
        if ((msgq_key = ftok(MSGQ_FILE_PATH, MSGQ_PROJ_ID)) == -1) {
                perror("ftok");
                exit(1);
        }
        if ((msgqid = msgget(msgq_key, PERMS)) == -1) {
                perror("msgget in child");
                exit(1);
        }
        printf("%d: Child has access to the msg queue\n",getpid());
    printf("USER PID: %d  PPID: %d  SysClockS: %d  SysClockNano: %d \n--Just Starting\n", getpid(), getppid(), shm_clock->seconds, shm_clock->nanosecond);


    MessageBuffer buf, rcvbuf;
    buf.mtype = getppid();
    buf.sender = getpid();


    while(true){
        if (TERMINATION_CHANCE > GenerateRandomNumber(0, 1000, getpid())){
            std::cout << "Child " << getpid() << " randomly terminating..." << std::endl;
            break;
        }

        int pageNumber = GenerateRandomNumber(0, 63, getpid());
        int offset = GenerateRandomNumber(0, 1023, getpid());
        buf.memoryAddress = (pageNumber * 1024) + offset;

        if(READ_CHANCE > GenerateRandomNumber(1, 100, getpid())){
            buf.msgCode = MSG_READ;
        } else {
            buf.msgCode = MSG_WRITE;
        }
        if(msgsnd(msgqid, &buf, sizeof(MessageBuffer), 1) == -1) {
            perror("msgsnd to parent failed\n");
            exit(1);
        }

        if(msgrcv(msgqid, &rcvbuf, sizeof(MessageBuffer), getpid(), 0) == -1) {
            perror("Failed to receive message\n");
            exit(1);
        }
        if(rcvbuf.msgCode == MSG_BLOCKED){
            if(msgrcv(msgqid, &rcvbuf, sizeof(MessageBuffer), getpid(), 0) == -1) {
                perror("Failed to receive message\n");
                exit(1);
            }
            if(rcvbuf.msgCode != MSG_GRANTED){
                perror("Child process unblocked but msgCode was not MSG_GRANTED");
                exit(1);
            }
        }
    }
    shmdt(shm_clock);
    printf("%d: Terminating Child\n",getpid());
    return EXIT_SUCCESS;
}
