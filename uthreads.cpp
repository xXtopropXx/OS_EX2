/**
 *
 */

#include "uthreads.h"
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/time.h>
#include <queue>
#include <iostream>

#define JB_SP 6
#define JB_PC 7

typedef void(*func)(void);
typedef unsigned long address_t;


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

class UThread{
public:
	int id = -1;
	int state;
	char stack[STACK_SIZE];
	int runningTime = 0;
	sigjmp_buf env;
	func f;
	address_t pc;
	address_t sp;
	UThread(){};

	UThread(int id, func f){
		this->id = id;
		this->f = f;
		pc = (address_t)f;
		sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
		sigsetjmp(env, 1);
		(env->__jmpbuf)[JB_SP] = translate_address(sp);
		(env->__jmpbuf)[JB_PC] = translate_address(pc);
		sigemptyset(&env->__saved_mask);
	}
};

UThread* threads[MAX_THREAD_NUM];
//sigjmp_buf env[MAX_THREAD_NUM];
std::queue<int> readyId;
std::queue<int> blockId;
int sleepId[MAX_THREAD_NUM];//TODO array or just signals
int runnigId;


void timer_handler(int sig)
{
  printf("Timer expired\n");
  //put the current running thread into the ready queue
  sigsetjmp(threads[runnigId]->env, 1);
  //TODO check if needed
  sigemptyset(&(threads[runnigId]->env)->__saved_mask);
  readyId.push(runnigId);
  if(!readyId.empty()){
	  runnigId = readyId.front();
	  readyId.pop();
  }
  //long jump to runnig id thread
  siglongjmp((threads[runnigId]->env),1);
}

void zeroFunction()
{
	for(;;){
		pause();
	}
}

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs){
	struct sigaction sa;
	struct itimerval timer;

	// Install timer_handler as the signal handler for SIGVTALRM.
	sa.sa_handler = &timer_handler;
	if (sigaction(SIGVTALRM, &sa,NULL) < 0) {
		printf("sigaction error."); //TODO maybe remove print
		return -1;
	}

	// configure the timer to expire every quantum_usecs in micro-seconds after that.
	timer.it_interval.tv_sec = 0;	// following time intervals, seconds part
	timer.it_interval.tv_usec = quantum_usecs;	// following time intervals, microseconds part

	// Start a virtual timer. It counts down whenever this process is executing.
	if (setitimer (ITIMER_VIRTUAL, &timer, NULL)) {
		printf("setitimer error."); //TODO maybe remove print
		return -1;
	}
	uthread_spawn(zeroFunction);
	//TODO jump to the zero function
	return 0;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void)){
	for(int index = 0; index < MAX_THREAD_NUM; index++){
		if(threads[index] == nullptr){
			//create the new thread
			UThread* newThread = new UThread(index, f);
			threads[index] = newThread;
			return 0;
		}
	}
	return -1;
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered as an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid);


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED or SLEEPING states has no
 * effect and is not considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid);


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state. Resuming a thread in the RUNNING, READY or SLEEPING
 * state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid);


/*
 * Description: This function puts the RUNNING thread to sleep for a period
 * of num_quantums (not including the current quantum) after which it is moved
 * to the READY state. num_quantums must be a positive number. It is an error
 * to try to put the main thread (tid==0) to sleep. Immediately after a thread
 * transitions to the SLEEPING state a scheduling decision should be made.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums);


/*
 * Description: This function returns the number of quantums until the thread
 * with id tid wakes up including the current quantum. If no thread with ID
 * tid exists it is considered as an error. If the thread is not sleeping,
 * the function should return 0.
 * Return value: Number of quantums (including current quantum) until wakeup.
*/
int uthread_get_time_until_wakeup(int tid);


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid();


/*
 * Description: This function returns the total number of quantums that were
 * started since the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums();


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered as an error.
 * Return value: On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid);



