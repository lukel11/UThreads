#ifndef OS2CPP_SINGLETHREAD_H
#define OS2CPP_SINGLETHREAD_H


#include <string>
#include <csetjmp>
#include "uthreads.h"
#include "iostream"
#define SYS_ERR 1
#define LB_ERR 0
using namespace std;
/**
 * @enum State
 * @brief thread, and IDs states
 */
typedef enum State{
    READY,RUNNING,BLOCKED, FREE, OCCUPIED
}State;

/**
 * @class SingleThread
 * @brief represents a single thread object
 */
class SingleThread{
public:
    int ID;
    int _thread_quantum;
    State _state;
    char* _stack;
    sigjmp_buf env;
    static size_t quantums;
    bool _mutex_blocked;
    /**
     * @constructor SingleThread
     * @param ID : thread ID
     * @brief allocates a Stack for each thread
     */
    SingleThread(int ID): ID(ID), _thread_quantum(0) ,_state(READY), _mutex_blocked(false)
    {
        _stack = new char[STACK_SIZE];
    }
    ~SingleThread()
    {
        delete[] _stack;
    }
    void increment_quantum()
    {
        _thread_quantum++;
        quantums++;
    }
};
size_t SingleThread::quantums = 0;
/**
 * @func manage_err
 * @brief handles error management
 * @param MSG : massege macro to print
 * @param ERR_SIG : error indicator - system | library
 */
void manage_err(string MSG, int ERR_SIG)
{
    if(ERR_SIG == SYS_ERR)
    {
        cerr << "system error: " << MSG << endl;
        exit(SYS_ERR);
    }
    cerr << "thread library error: " << MSG << endl;
}
#endif //OS2CPP_SINGLETHREAD_H
