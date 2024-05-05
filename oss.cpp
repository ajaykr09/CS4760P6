#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <string.h>
#include <signal.h>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <fstream>
#include <chrono>
#include <queue>
#include <random>
using namespace std;

// Constants for system configuration
#define MSG_WRITE 2
#define MSG_READ 1
#define TOTAL_RESOURCES 10
#define TOTAL_INSTANCES 20
#define DISPATCH_AMOUNT 1e7 // 10 ms
#define CHILD_LAUNCH_AMOUNT 1000
#define UNBLOCK_AMOUNT 1000
#define MSGQ_FILE_PATH "msgq.txt"
#define MSGQ_PROJ_ID 65
#define PERMS 0644
#define MSG_GRANTED 4
#define MSG_BLOCKED 3

// Structures for system operation
typedef struct SystemClock {
    int seconds;
    int nanoseconds;
} SystemClock;

// Structures for PCB
struct ProcessControlBlock {
    int isOccupied;
    pid_t pid;
    int startSecs;
    int startNanos;
    int blocked;
    int resourcesHeld[TOTAL_RESOURCES];
};

// Structures for Page Table Entry
struct PageTableEntry{
    pid_t pid;
    int pageNumber;
    bool secondChanceBit;
    bool dirtyBit;
};

// Structures for Message Buffer
typedef struct MessageBuffer {
        long mtype;
        int msgCode;
        int memoryAddress;
        pid_t sender;
} MessageBuffer;

// Function Prototypes
// Initializes the process table to default values
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

// Finds an empty slot in the process table
int FindEmptyProcessSlot(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (!processTable[i].isOccupied){
            return (i + 1);
        }
    }
    return 0;
}

// Counts the number of active processes in the table
int CountActiveProcesses(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    int numProcesses = 0;
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied){
            numProcesses++;
        }
    }
    return (numProcesses == 0) ? 1 : numProcesses; // Ensures never zero to prevent divide by zero error
}

// Checks if the process table is empty
bool IsProcessTableEmpty(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied){
            return 0;
        }
    }
    return 1;
}

// Checks if all processes are blocked
bool AreAllProcessesBlocked(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].isOccupied && !processTable[i].blocked){
            return 0;
        }
    }
    return 1;
}

// Displays the process table in the output file
void DisplayProcessTable(ProcessControlBlock processTable[], int maxSimultaneousProcesses, int seconds, int nanoseconds, std::ostream& outputFile){
    static int next_print_secs = 0;
    static int next_print_nanos = 0;

    if(seconds > next_print_secs || (seconds == next_print_secs && nanoseconds > next_print_nanos)){
        printf("OSS PID: %d  SysClockS: %d  SysClockNano: %d  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n", getpid(), seconds, nanoseconds);
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << seconds << "  SysClockNano " << nanoseconds << "  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n";
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

// Removes a process from the table upon termination
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

// Updates a process as blocked in the table
void UpdateBlockedProcess(ProcessControlBlock processTable[], pid_t pid, int maxSimultaneousProcesses, int blockedUntilSec, int blockedUntilNano){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].pid == pid){

            processTable[i].blocked = 1;
            return;
        }
    }
}

// Terminates all processes when the system is cleaning up
void TerminateAllProcesses(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].isOccupied){
            kill(processTable[i].pid, SIGKILL);
        }
    }
}

// Checks if a specific process ID is present in the process table
bool IsProcessPresent(ProcessControlBlock processTable[], int maxSimultaneousProcesses, pid_t pid){
    for(int i = 0; i < maxSimultaneousProcesses; i++){
        if(processTable[i].pid == pid){
            return true;
        }
    }
    return false;
}

// Returns the index of a process in the table based on its PID
int GetProcessIndex(ProcessControlBlock processTable[], int maxSimultaneousProcesses, pid_t pid){
    for (int i = 0; i < maxSimultaneousProcesses; i++){
        if (processTable[i].pid == pid){
            return i;
        }
    }
    return -1;
}

// Send a message to a child process via message queue
void SendMessageToProcess(MessageBuffer);

const int FRAME_TABLE_SIZE = 256;
int memoryAccesses = 0;
int pageFaults = 0;

// Initializes the frame table to default values
void InitializePageTable(PageTableEntry frameTable[]){
    for(int i = 0; i < FRAME_TABLE_SIZE; i++){
        frameTable[i].pid = 0;
        frameTable[i].pageNumber = 0;
        frameTable[i].secondChanceBit = 0;
        frameTable[i].dirtyBit = 0;
    }
}

// Displays the page table in the output file
void DisplayPageTable(PageTableEntry frameTable[], int seconds, int nanoseconds, std::ostream& outputFile){
    static int next_print_secs = 0;
    static int next_print_nanos = 0;

    if(seconds > next_print_secs || (seconds == next_print_secs && nanoseconds > next_print_nanos)){
        std::cout << "OSS PID: " << getpid() << "  SysClockS: " << seconds << "  SysClockNano " << nanoseconds << "  \nPage Table:\n\tOwner PID\tPage Number\t2nd Chance Bit\tDirty Bit\n";
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << seconds << "  SysClockNano " << nanoseconds << "  \nPage Table:\n\tOwner PID\tPage Number\t2nd Chance Bit\tDirty Bit\n";
        for(int i = 0; i < FRAME_TABLE_SIZE; i++){
            std::cout << "Frame " << std::to_string(i + 1) << ":\t" << std::to_string(frameTable[i].pid) << "\t" << std::to_string(frameTable[i].pageNumber) << "\t" << std::to_string(frameTable[i].secondChanceBit) << "\t" << std::to_string(frameTable[i].dirtyBit) << std::endl;
            outputFile << std::to_string(i + 1) << "\t" << std::to_string(frameTable[i].pid) << "\t" << std::to_string(frameTable[i].pageNumber) << "\t" << std::to_string(frameTable[i].secondChanceBit) << "\t" << std::to_string(frameTable[i].dirtyBit) << std::endl;
        }
        next_print_nanos = next_print_nanos + 500000000;
        if (next_print_nanos >= 1000000000){
            next_print_nanos = next_print_nanos - 1000000000;
            next_print_secs++;
        }
    }
}

// Handles a page fault by selecting a victim frame and swapping pages
void HandlePageFault(PageTableEntry frameTable[], std::ofstream* outputFile, pid_t pid, int pageNumber, int msgCode){
    static int victimFrame = 0;
    bool victimFound = false;

    while(!victimFound){
        if(frameTable[victimFrame].secondChanceBit == 1){
            frameTable[victimFrame].secondChanceBit = 0;
        } else {
            victimFound = true;
            if(frameTable[victimFrame].dirtyBit){
                std::cout << "OSS: Swapping out dirty frame, saving to secondary storage..." << std::endl;
                *outputFile << "OSS: Swapping out dirty frame, saving to secondary storage..." << std::endl;
            }

            frameTable[victimFrame].pid = pid;
            frameTable[victimFrame].pageNumber = pageNumber;
            frameTable[victimFrame].secondChanceBit = 1;
            frameTable[victimFrame].dirtyBit = (msgCode == MSG_WRITE) ? 1 : 0;

            MessageBuffer buf;
            buf.mtype = pid;
            buf.sender = getpid();
            buf.memoryAddress = pageNumber;
            buf.msgCode = MSG_GRANTED;
            memoryAccesses++;
            SendMessageToProcess(buf);
        }
        victimFrame++;
        if(victimFrame == FRAME_TABLE_SIZE){
            victimFrame = 0;
        }
    }
}

// Increment the system clock
void IncrementClock(SystemClock* c, int increment_amount){
    c->nanoseconds = c->nanoseconds + increment_amount;
    if (c->nanoseconds >= 1e9){
        c->nanoseconds -= 1e9;
        c->seconds++;
    }
}

// Handles page requests from processes
void HandlePageRequest(PageTableEntry frameTable[], std::ofstream* outputFile, SystemClock* c, pid_t pid, int memoryAddress, int msgCode){

    MessageBuffer buf;
    buf.mtype = pid;
    buf.sender = getpid();
    buf.memoryAddress = memoryAddress;
    int pageNumber = memoryAddress/1024;


    for (int i = 0; i < FRAME_TABLE_SIZE; i++){
        if(frameTable[i].pid == pid && frameTable[i].pageNumber == pageNumber){

            if(msgCode == MSG_WRITE)
                frameTable[i].dirtyBit = 1;
            frameTable[i].secondChanceBit = 1;
            IncrementClock(c, 100);

            buf.msgCode = MSG_GRANTED;
            memoryAccesses++;
            SendMessageToProcess(buf);
            return;
        }
    }

    buf.msgCode = MSG_BLOCKED;
    SendMessageToProcess(buf);
    HandlePageFault(frameTable, outputFile, pid, pageNumber, msgCode);
    pageFaults++;
}

// Adds specified nanoseconds to the provided time, adjusting seconds if necessary
void timeAddition(int *addend1Secs, int *addend1Nanos, int addend2Nanos){
    *addend1Nanos += addend2Nanos;
    if (*addend1Nanos >= 1e9){
        *addend1Nanos -= 1e9;
        *addend1Secs++;
    }
}

// Generates a random number using a seed modifier for process-specific randomness
int GenerateRandomNumber(int min, int max, int pid) {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count() * pid;
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(min, max);
    int random_number = distribution(generator);
    return random_number;
}


void LaunchProcess(ProcessControlBlock[], int);
bool IsLaunchIntervalMet(int);
void HandleTimeout(int);
void HandleInterrupt(int);
void CleanupSystem(std::string);
void OutputStats(double);

// Signal handling global
volatile sig_atomic_t term = 0;

// Global structures for process and page tables (not shared memory)
struct ProcessControlBlock processTable[20];
struct PageTableEntry frameTable[FRAME_TABLE_SIZE]; // Frame table size fixed at 256

// Global variables for system management
SystemClock* shm_clock;
key_t clock_key = ftok("/tmp", 35);
int shmtid = shmget(clock_key, sizeof(SystemClock), IPC_CREAT | 0666);
std::ofstream outputFile;
int msgqid;
int maxSimultaneousProcesses = 1;
int successfulTerminations = 0;
std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

// Main function with argument parsing and system initialization
int main(int argc, char** argv){
    int option, numberOfChildren = 1, launchInterval = 100;
    int totalChildren;
    double totalBlockedTime = 0, totalCPUTime = 0, totalTimeInSystem = 0;
    string logFileName = "logFileName.txt";
    while ( (option = getopt(argc, argv, "hn:s:i:f:")) != -1) {
        switch(option) {
            case 'h':
                printf(" [-n proc] [-s simul] [-t timelimitForChildren]\n"
 "[-i intervalInMsToLaunchChildren] [-f logFileName]");
                return 0;
                break;
            case 'n':
                numberOfChildren = atoi(optarg);
                totalChildren = numberOfChildren;
                break;
            case 's':
                maxSimultaneousProcesses = atoi(optarg);
                break;
            case 'i':
                launchInterval = (1000000 * atoi(optarg));
                break;
            case 'f':
                logFileName = optarg;
                break;
        }
        }

    // Initialize signal handlers and start system clock
    std::signal(SIGALRM, HandleTimeout);
    std::signal(SIGINT, HandleInterrupt);
    alarm(5);

    InitializeProcessTable(processTable);
    InitializePageTable(frameTable);
    shm_clock = (SystemClock*)shmat(shmtid, NULL, 0);
    shm_clock->seconds = 0;
    shm_clock->nanoseconds = 0;

    outputFile.open(logFileName);
    if (!outputFile.is_open()) {
        std::cerr << "Error: Unable to open logFileName" << std::endl;
        return 1;
    }
        // Initialize message queue
        key_t msgq_key;
        system("touch msgq.txt");
        if ((msgq_key = ftok(MSGQ_FILE_PATH, MSGQ_PROJ_ID)) == -1) {
                perror("ftok");
                exit(1);
        }
        if ((msgqid = msgget(msgq_key, PERMS | IPC_CREAT)) == -1) {
                perror("msgget in parent");
                exit(1);
        }
        cout << "OSS: Message queue set up\n";
    outputFile << "OSS: Message queue set up\n";
    // Main loop for child process management and system monitoring
    while(numberOfChildren > 0 || !IsProcessTableEmpty(processTable, maxSimultaneousProcesses)){
        if(numberOfChildren > 0 && IsLaunchIntervalMet(launchInterval) && FindEmptyProcessSlot(processTable, maxSimultaneousProcesses)){
            std::cout << "OSS: Launching Child Process..." << endl;
            outputFile << "OSS: Launching Child Process..." << endl;
            numberOfChildren--;
            LaunchProcess(processTable, maxSimultaneousProcesses);
        }

        pid_t pid = waitpid((pid_t)-1, nullptr, WNOHANG); // Non-blocking wait for child termination
        if (pid > 0){
            std::cout << "OSS: Receiving child " << pid << " has terminated! Releasing childs' resources..." << std::endl;

            int i = GetProcessIndex(processTable, maxSimultaneousProcesses, pid);

            if(processTable[i].isOccupied){
                RemoveProcessFromTable(processTable, pid, maxSimultaneousProcesses);
            }
            pid = 0;
        }

        MessageBuffer rcvbuf;
        if (msgrcv(msgqid, &rcvbuf, sizeof(MessageBuffer), getpid(), IPC_NOWAIT) == -1) {
            if (errno != ENOMSG){
                perror("Error: failed to receive message in parent\n");
                CleanupSystem("perror encountered.");
                exit(1);
            }
        }
        if(rcvbuf.msgCode == -1){
            std::cout << "OSS: Checked and found no messages for OSS in the msgqueue." << std::endl;
        } else if(rcvbuf.msgCode == MSG_READ || rcvbuf.msgCode == MSG_WRITE){
            std::cout << "OSS: " << rcvbuf.sender << " requesting read/write of address " << rcvbuf.memoryAddress << " at time " << shm_clock->seconds << ":" << shm_clock->nanoseconds << std::endl;
            HandlePageRequest(frameTable, &outputFile, shm_clock, rcvbuf.sender, rcvbuf.memoryAddress, rcvbuf.msgCode);
        }

        IncrementClock(shm_clock, DISPATCH_AMOUNT);
        DisplayProcessTable(processTable, maxSimultaneousProcesses, shm_clock->seconds, shm_clock->nanoseconds, outputFile);
        DisplayPageTable(frameTable, shm_clock->seconds, shm_clock->nanoseconds, outputFile);
        std::cout << "Looping!" << std::endl;
    }

        std::cout << "OSS: Child processes have completed. (" << numberOfChildren << " remaining)\n";
    std::cout << "OSS: Parent is now ending.\n";
    outputFile << "OSS: Child processes have completed. (" << numberOfChildren << " remaining)\n";
    outputFile << "OSS: Parent is now ending.\n";
    outputFile.close();

    CleanupSystem("Shutting down OSS.");

    return 0;
}

// Implementations of helper functions for process and system management
void SendMessageToProcess(MessageBuffer buf){
    if (msgsnd(msgqid, &buf, sizeof(MessageBuffer), 0) == -1) {
        perror("msgsnd to child failed\n");
        exit(1);
    }
}

// Launches a child process and updates the process table
void LaunchProcess(ProcessControlBlock processTable[], int maxSimultaneousProcesses){
    pid_t childPid = fork();
    if (childPid == 0) {
        execl("./user", "./user", nullptr);
        perror("LaunchProcess(): execl() has failed!");
        exit(EXIT_FAILURE);
    } else if (childPid == -1) {
        perror("Error: Fork has failed");
        exit(EXIT_FAILURE);
    } else {
        int i = (FindEmptyProcessSlot(processTable, maxSimultaneousProcesses) - 1);
        processTable[i].isOccupied = 1;
        processTable[i].pid = childPid;
        processTable[i].startSecs = shm_clock->seconds;
        processTable[i].startNanos = shm_clock->nanoseconds;
        processTable[i].blocked = 0;
        for(int j = 0; j < TOTAL_RESOURCES; j++){
            processTable[i].resourcesHeld[j] = 0;
        }
        IncrementClock(shm_clock, CHILD_LAUNCH_AMOUNT);
    }
}


// Checks if the launch interval is met to start a new process
bool IsLaunchIntervalMet(int launchInterval){
    static int last_launch_secs = 0;
    static int last_launch_nanos = 0;

    int elapsed_secs = shm_clock->seconds - last_launch_secs;
    int elapsed_nanos = shm_clock->nanoseconds - last_launch_nanos;

    while (elapsed_nanos < 0) {
        elapsed_secs--;
        elapsed_nanos += 1000000000;
    }

    if (elapsed_secs > 0 || (elapsed_secs == 0 && elapsed_nanos >= launchInterval)) {
        last_launch_secs = shm_clock->seconds;
        last_launch_nanos = shm_clock->nanoseconds;
        return true;
    } else {
        return false;
    }
}

// Signal handler for system timeout
void HandleTimeout(int signum) {
    CleanupSystem("Timeout Occurred.");
}

// Signal handler for Ctrl+C interruption
void HandleInterrupt(int signum) {
    CleanupSystem("Ctrl+C detected.");
}

// Outputs statistics and finalizes system shutdown
void OutputStats(double duration){
    std::cout << "\nFinal Report" << std::endl;
    std::cout << "Number of Faults: " << pageFaults << std::endl;
    std::cout << "Number of Memory Accesses: " << memoryAccesses << std::endl;
    std::cout << "Number of Memory Accesses per second: " << std::fixed << std::setprecision(1) << static_cast<double>(memoryAccesses)/duration << std::endl;
    std::cout << "Average Number of Faults per Memory Access: " << std::fixed << std::setprecision(1) << static_cast<double>(pageFaults)/memoryAccesses << std::endl;

    outputFile << "\nRUN RESULT REPORT" << std::endl;
    outputFile << "Number of PageTableEntry Faults: " << pageFaults << std::endl;
    outputFile << "Number of Memory Accesses: " << memoryAccesses << std::endl;
    outputFile << "Number of Memory Accesses per second: " << std::fixed << std::setprecision(1) << static_cast<double>(memoryAccesses)/duration << std::endl;
    outputFile << "Average Number of PageTableEntry Faults per Memory Access: " << std::fixed << std::setprecision(1) << static_cast<double>(pageFaults)/memoryAccesses << std::endl;
}

// Cleans up system resources and prepares for shutdown
void CleanupSystem(std::string cause) {
    std::cout << cause << " Cleaning up" << std::endl;
    outputFile << cause << " Cleaning up" << std::endl;
    TerminateAllProcesses(processTable, maxSimultaneousProcesses);
    outputFile.close();
    shmdt(shm_clock);
    if (msgctl(msgqid, IPC_RMID, NULL) == -1) {
                perror("Error: msgctl to get rid of queue in parent failed");
                exit(1);
        }

    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    OutputStats(static_cast<double>(duration.count()));

    std::exit(EXIT_SUCCESS);
}
