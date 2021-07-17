#include "uthreads.h"
#include <cstdio>
#include <iostream>
#include "deque"
#include "unordered_map"
#include "array"
#include <unistd.h>
#include "SingleThread.h"
#include "vector"
#include <csignal>
#include "sys/time.h"
/**=======================setup==========================*/
using namespace std;
typedef pair<bool,int> mutex;
typedef unsigned long address_t;

/**threads map - contains all thread instances inside*/
unordered_map<int, SingleThread*> threads;
/** shceduling queues*/
deque<int> ready_q = deque<int>();
deque<int> blocked_q = deque<int>();
deque<int> mutex_blocked_q = deque<int>();
/** ID array*/
array<State ,MAX_THREAD_NUM> ID_array;
/** pointer to running thread*/
SingleThread* running_thread = nullptr;
size_t thread_quantum;
/** mutex object*/
mutex thread_mutex = pair<bool,int>(false, -1);
struct itimerval running_timer;
struct sigaction signal_action;
#define MICRO_SEC 1000000
/********* ERROR MACROS & INDICATORS*********/
#define BAD_ALLOC "Failed to Allocate Thread Stack/Object."
#define VTIMER_FAIL "Failed to set virtual timer."
#define SIGACT_FAIL "Sigaction error"
#define QUANTUM_ERR "Quantum-usecs must be a non-negative number."
#define MAX_CAPACITY_REACH "Cannot assign ID to thread,Maximum thread number reached."
#define ILLEGAL_TID "Illegal ID passed to function."
#define MUTEX_ERR "Cannot lock/unlock mutex from the same thread twice."
/*** used for organizing***/
#define MUTEX_BLK_Q 2
#define READYQ 1 // indicator to point for ready deuqe
#define WAITINGQ 0 // inbdicator to point for waiting deque
#define MUTEX_FREE_STATE -1
/**=======================address translation==========================*/
#ifdef __x86_64__
/* code for 64 bit Intel arch */

#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif
/**===============================================================*/
/**
 * @func setup_env
 * @brief sets up the env variable for threads
 * @param thread
 * @param f : thread function to run
 */
void setup_env(SingleThread* thread, void (*f)(void))
{
    address_t sp, pc;
    sp = (address_t) thread->_stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t)f;
    sigsetjmp(thread->env, 1);
    ((thread->env)->__jmpbuf)[JB_SP] = translate_address(sp);
    ((thread->env)->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&(thread->env)->__saved_mask);
}
/**
 * @func assignID
 * @brief the smallest-assignable ID
 * @return ID, -1 on full capacity/fail
 */
int assignID()
{
    for(int i = 0; i < MAX_THREAD_NUM; i++)
    {
        if (ID_array[i] == FREE)
        {
            ID_array[i] = OCCUPIED;
            return i; // ID starts from 1
        }
    }
    return -1;
}
/**
 * @func freeID
 * @brief free's ID, for next thread that will be able to take it
 * @param id : thread ID
 */
void freeID(int id)
{
    if(id >= 0)
    {
        ID_array[id] = FREE;
    }
}
/**
 * @func isAssigned
 * @brief checks whether an ID is assigned
 * @param id : thread ID
 * @return
 */
bool isAssigned(int id)
{
    if(0 <= id && id <= 99)
    {
        return ID_array[id] == OCCUPIED;
    }
    return false;
}
/**
 * @func start_timer
 * @brief starts the virtual timer
 */
void start_timer()
{
    if(setitimer(ITIMER_VIRTUAL, &running_timer, nullptr) == -1)
    {
        manage_err(VTIMER_FAIL, SYS_ERR);
    }
}
/**
 * @func quantum expire Context Switch
 * @brief performs context changes
 */
void context_change(int signum)
{
    if(running_thread) // if thread hasn't terminated itself
    {
        if(sigsetjmp(running_thread->env,1)){return;}
        if(running_thread->_state != BLOCKED && !running_thread->_mutex_blocked) // if context change is not a blocking scenario
        {
            running_thread->_state = READY; // transform from running to ready
            ready_q.push_back(running_thread->ID);
        }
    }
    running_thread  = threads[ready_q.front()];
    ready_q.pop_front();
    running_thread->_state = RUNNING;
    running_thread->increment_quantum();
    siglongjmp(running_thread->env,1);
}
/**
 * @func kill_process
 * @brief handles the termination of the main thread
 * frees allocated resources
 */
void kill_process()
{
    for(auto& thread : threads)
    {
        delete thread.second;
    }
    exit(0);
}
/**
 * @func delete from queue
 * @brief deletes tid from any queue requested
 * @param indicator : queue indicator (macro)
 * @param tid : thread ID
 */
void delete_from_queue(int indicator, int tid)
{
    deque<int>* thread_queue;

    switch(indicator)
    {
        case READYQ :{thread_queue = &ready_q;break;}
        case WAITINGQ :{thread_queue = &blocked_q;break;}
        default:thread_queue = &mutex_blocked_q;
    }
    for(auto it = thread_queue->begin(); it != thread_queue->end(); it++)
    {
        if(*it == tid)
        {
            thread_queue->erase(it);
            break;
        }
    }
}
/**
 * @func delete_thread
 * @brief deletes a thread, and frees its resources
 * @param tid - thread ID
 */
void delete_thread(int tid)
{
    delete threads[tid]; // delete pointer
    threads.erase(tid);
}
/**
 * @API_func uthread_init
 * @param quantum_usecs : usecs to assign each thread to run
 * @return
 */
int uthread_init(int quantum_usecs) {

    signal_action.sa_handler = context_change;
    if(sigemptyset (&signal_action.sa_mask) == -1)
    {
        manage_err(SIGACT_FAIL, SYS_ERR);
    }
    if(sigaddset(&signal_action.sa_mask, SIGVTALRM))
    {
        manage_err(SIGACT_FAIL, SYS_ERR);
    }
    if(sigaction(SIGVTALRM, &signal_action, nullptr))
    {
        manage_err(SIGACT_FAIL, SYS_ERR);
    }
    if(quantum_usecs <= 0)
    {
        manage_err(QUANTUM_ERR, LB_ERR);
        return -1;
    }
    for (State & id : ID_array)
    {
        id = FREE;
    }
    thread_quantum = quantum_usecs;
    try
    {
        running_thread = new SingleThread(assignID()); // assigns ID 0 to the main thread
    }
    catch(exception& exception)
    {
        manage_err(BAD_ALLOC, SYS_ERR);
    }
    threads[0] = running_thread; // assign main thread
    running_thread->_state = RUNNING;
    running_thread->increment_quantum();
    running_timer.it_value.tv_sec = running_timer.it_interval.tv_sec = (int) thread_quantum / MICRO_SEC;
    running_timer.it_value.tv_usec = running_timer.it_interval.tv_usec = (int) thread_quantum % MICRO_SEC;
    start_timer();
    return 0;
}
/**
 * @API_func uthread_spawn
 * @brief spawns a thread with entry function f
 * @param f
 * @return 0 success, -1 fail
 */
int uthread_spawn(void (*f)(void))
{
    int id = assignID();
    SingleThread* new_thread;
    if(id >= 1)
    {
        try {
            new_thread = new SingleThread(id);
        }
        catch(const exception& e) {
            manage_err(BAD_ALLOC, SYS_ERR);
        }
        setup_env(new_thread, f);
        threads[id] = new_thread;
        ready_q.push_back(id);
        return id;
    }
    else
    {
        manage_err(MAX_CAPACITY_REACH,LB_ERR);
    }
    return -1;
}
/**
 * @API_func uhread_terminate
 * @brief terminates a thread
 * @param tid : thread ID
 * @return
 */
int uthread_terminate(int tid)
{
    if(isAssigned(tid))
    {
        sigprocmask(SIG_BLOCK, &signal_action.sa_mask, NULL);
        freeID(tid);
        if(tid == 0) // if kill process
        {
            kill_process();
        }
        if (thread_mutex.second == tid) // if the thread to be terminated locked mutex
        {
            uthread_mutex_unlock(); // unlock
        }
        if(running_thread->ID == tid) // if running thread kill itself
        {
            delete_thread(tid);
            running_thread = nullptr;
            context_change(0);
        }
        delete_thread(tid);
        threads[tid]->_state == READY ? delete_from_queue(READYQ,tid) :
        delete_from_queue(WAITINGQ,tid);
        sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
        return 0;
    }
    manage_err(ILLEGAL_TID, LB_ERR);
    return -1;
}
/**
 * @API_func uthread_block
 * @brief blocks a thread and moves it to waiting_q
 * @param tid : thread ID
 * @return 0 success, -1 fail
 */
int uthread_block(int tid) {
    sigprocmask(SIG_BLOCK, &signal_action.sa_mask, NULL);
    if(!isAssigned(tid) || tid == 0)
    {
        manage_err(ILLEGAL_TID, LB_ERR);
        sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
        return -1;
    }
    if(threads[tid]->_state == READY)
    {
        if(!threads[tid]->_mutex_blocked) // if he's in ready_q and not in mutex_blocked_q
        {
            delete_from_queue(READYQ, tid);
        }
        threads[tid]->_state = BLOCKED;
        blocked_q.push_back(tid);
    }
    if(running_thread->ID == tid)
    {
        running_thread->_state = BLOCKED;
        blocked_q.push_back(tid);
        context_change(0);
    }
    sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
    return 0;
}
/**
 * @API_func uthread_resume
 * @brief resumes a thread from blocked state
 * @param tid
 * @return 0 success, -1 fail
 */
int uthread_resume(int tid) {
    if(!isAssigned(tid))
    {
        manage_err(ILLEGAL_TID, LB_ERR);
        return -1;
    }
    delete_from_queue(WAITINGQ, tid);
    if(!threads[tid]->_mutex_blocked)
    {
        ready_q.push_back(tid);
    }
    threads[tid]->_state = READY;
    return 0;
}
/**
 * @API_func uthread_mutex_lock
 * @brief locks mutex if possible, else moves thread to blocked queue (waiting_q)
 * @return 0 success, -1 fail
 */
int uthread_mutex_lock()
{
    sigprocmask(SIG_BLOCK, &signal_action.sa_mask, NULL);
    if(running_thread->ID == thread_mutex.second) // if trying to lock mutex *again*
    {
        manage_err(MUTEX_ERR, LB_ERR);
        sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
        return -1;
    }
    //if locked - add to mutex_blocked_q
    if(thread_mutex.first)
    {
        running_thread->_state = READY;
        running_thread->_mutex_blocked = true;
        mutex_blocked_q.push_back(running_thread->ID);
        context_change(0);
        if(!thread_mutex.first) // if after context change no one else locked mutex
        {
            //lock it so there wont be starvation
            thread_mutex.first = true; // lock mutex
            thread_mutex.second = running_thread->ID;
        }
    }
    else
    {
        thread_mutex.first = true; // lock mutex
        thread_mutex.second = running_thread->ID;
    }
    sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
    return 0;
}
/**
 * @API_func uthread_mutex_unlock
 * @brief unlocks mutex and moves one thread from mutex_locked to ready queue
 * @return 0 sucess, -1 fail
 */
int uthread_mutex_unlock()
{
    sigprocmask(SIG_BLOCK, &signal_action.sa_mask, NULL);
    if(!thread_mutex.first) // if already unlocked
    {
        manage_err(MUTEX_ERR, LB_ERR);
        sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
        return -1;
    }
    if(running_thread->ID == thread_mutex.second) // if thread that locked requested unlock
    {
        thread_mutex.first = false; // unlock mutex
        thread_mutex.second = MUTEX_FREE_STATE;
        if(!mutex_blocked_q.empty())
        {
            int new_tid = mutex_blocked_q.front(); // get front thread tid
            delete_from_queue(MUTEX_BLK_Q, new_tid);
            threads[new_tid]->_mutex_blocked = false;
            if(threads[new_tid]->_state != BLOCKED)
            {
                ready_q.push_back(new_tid);
            }
        }
    }
    sigprocmask(SIG_UNBLOCK, &signal_action.sa_mask, NULL);
    return 0;
}
/**
 * @API_func uthread_get_tid
 * @return running thread ID
 */
int uthread_get_tid() {
    return running_thread->ID;
}
/**
 * @API_func uthread_get_total_quantums
 * @return total quantums
 */
int uthread_get_total_quantums() {
    return (int) SingleThread::quantums;
}
/**
 * @API_func uthread_get_quantums
 * @param tid : thread ID
 * @return thread quantums
 */
int uthread_get_quantums(int tid) {
    if(isAssigned(tid))
    {
        return threads[tid]->_thread_quantum;
    }
    manage_err(ILLEGAL_TID,LB_ERR);
    return -1;
}

















