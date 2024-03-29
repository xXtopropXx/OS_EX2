/**
 *	EX2
 *	itaygera
 *	nadavost
 */

#include "uthreads.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/time.h>
#include <list>
#include <iostream>

#define JB_SP 6
#define JB_PC 7
#define READY 2
#define RUNNING 1
#define SLEEPING 3
#define BLOCKED 4
#define SEC_TO_USEC 1000000

typedef void (*func)(void);
typedef unsigned long address_t;

/*
 * print the content of a given list
 * for debugging
 */
void print(const std::list<int>& s) {
	std::list<int>::const_iterator i;
	for (i = s.begin(); i != s.end(); ++i)
		std::cout << *i << ",";
	std::cout << std::endl;
}

/* A translation is required when using an address of a variable.
 Use this as a black box in your code. */
address_t translate_address(address_t addr) {
	address_t ret;
	asm volatile("xor    %%fs:0x30,%0\n"
			"rol    $0x11,%0\n"
			: "=g" (ret)
			: "0" (addr));
	return ret;
}

/*
 * unblocking a signal from the type: SIGVALM
 * @param sigset_t& set: the set that will
 * assigned and blocked
 */
void unBlockAlarmSignal(sigset_t& set) {
	//unblocked signals: SIGVTALRM
	sigemptyset(&set);
	sigaddset(&set, SIGVTALRM);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/*
 * blocking a signal from the type: SIGVALM
 */
void blockAlarmSignal(sigset_t& set) {
	//blocked signals: SIGVTALRM
	sigemptyset(&set);
	sigaddset(&set, SIGVTALRM);
	sigprocmask(SIG_BLOCK, &set, NULL);
}

/**
 * the class that represents a thread
 *
 */
class UThread {
public:
	int id = -1;
	int state;
	char stack[STACK_SIZE];
	int runningTime = 0;
	int remainingSleepQuantum = 0;
	sigjmp_buf env;
	func f = nullptr;
	address_t pc = 0;
	address_t sp = 0;
	UThread() {
		this->id = 0;
		this->f = nullptr;
		this->state = READY;
	}

	UThread(int id, func f) {
		this->id = id;
		this->f = f;
		this->state = READY;
		pc = (address_t) f;
		sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
		int ret = sigsetjmp(env, 1);
		if (ret == 1) {
			return;
		}
		(env->__jmpbuf)[JB_SP] = translate_address(sp);
		(env->__jmpbuf)[JB_PC] = translate_address(pc);
		sigemptyset(&env->__saved_mask);
	}
};

static UThread* threads[MAX_THREAD_NUM];
static std::list<int> readyId;
static std::list<int> blockId;
static std::list<int> sleepId;
static int runningId = 0;
static int totalQuantums = 1;
static int qunatumSize = 0;

/*
 * delete a thread from a given list
 */
void eraseFromList(std::list<int>& list, int id) {
	std::list<int>::iterator it;
	it = list.begin();
	for (; it != list.end(); it++) {
		if (*it == id) {
			list.erase(it);
			return;
		}
	}
}

/*
 * reset the timer at the end of each thread switching
 */
void resetTimer() {
	struct itimerval time;
	getitimer(ITIMER_VIRTUAL, &time);
	time.it_value.tv_usec = qunatumSize;
	setitimer(ITIMER_VIRTUAL, &time, NULL);
}

/*
 * The SIGVTALM signal handler - The thread swapper
 */
void switchThreads(int sig) {
	//Unwanted warning - left in order to give us extensibility in the future
	(void) sig;
	//put the current running thread into the ready queue if not terminated
	if (threads[runningId] != nullptr) {
		int ret = sigsetjmp(threads[runningId]->env, 1);
		if (ret == 1) {
			return;
		}
	}
	//ignore SIGVTALRM signal
	struct sigaction ign;
	struct sigaction oldHandler;
	ign.sa_handler = SIG_IGN;
	sigaction(SIGVTALRM, &ign, &oldHandler);

	totalQuantums++;

	//Check the sleeping threads and handle them
	std::list<int>::iterator iter = sleepId.begin();
	while (iter != sleepId.end()) {

		if (threads[*iter]->remainingSleepQuantum == 0) {
			int toWake = *iter;
			++iter;
			// alternatively, i = items.erase(i);
			eraseFromList(sleepId, toWake);
			readyId.push_back(toWake);
			threads[toWake]->state = READY;

		} else {
			threads[*iter]->remainingSleepQuantum--;
			++iter;
		}
	}

	// if thread was not blocked, terminated or got sleep,
	// add it to  the ready list
	if (threads[runningId] != nullptr) {
		if (threads[runningId]->state == RUNNING) {
			readyId.push_back(runningId);
			threads[runningId]->state = READY;
		}
	}
	//get the next thread
	runningId = readyId.front();
	threads[runningId]->state = RUNNING;
	readyId.pop_front();

	threads[runningId]->runningTime++;
	//reset the timer in order to give the next thread a full quantum
	resetTimer();
	//stop ignoring the SIGVTALRM
	sigaction(SIGVTALRM, &oldHandler, NULL);

	//long jump to the next id thread
	siglongjmp((threads[runningId]->env), 1);
}

/*
 * delete a single thread from memory and process
 */
void deleteThread(int tid) {

	if (threads[tid]->state == BLOCKED) {
		eraseFromList(blockId, tid);
	} else if (threads[tid]->state == SLEEPING) {
		eraseFromList(sleepId, tid);
	} else if (threads[tid]->state == READY) {
		eraseFromList(readyId, tid);
	}
	delete threads[tid];
	threads[tid] = nullptr;

}

/*
 * delete all threads when calling terminate(0)
 */
void deleteAllThreads() {
	int i = 0;
	for (; i < MAX_THREAD_NUM; i++) {
		if (threads[i] != nullptr) {
			deleteThread(i);
		}
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
int uthread_init(int quantum_usecs) {
	qunatumSize = quantum_usecs;
	struct sigaction sa;
	struct itimerval timer;

	// Install timer_handler as the signal handler for SIGVTALRM.
	sa.sa_handler = &switchThreads;
	if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
		std::cerr << "system error: sigaction error, can't update handler"
				<< std::endl;
		return -1;
	}
	// first time interval, seconds part
	timer.it_value.tv_sec = 0;
	// first time interval, microseconds part
	timer.it_value.tv_usec = quantum_usecs;

	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	//create the main thread object
	UThread* newThread = new UThread();
	threads[0] = newThread;
	newThread->id = 0;
	newThread->runningTime = 1;
	newThread->state = RUNNING;
	int ret = sigsetjmp(newThread->env, 1);
	if (ret == 1) {
		return 0;
	}
	sigemptyset(&newThread->env->__saved_mask);

	// Start a virtual timer. Counts down whenever this process is executing.
	if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
		std::cerr << "system error: setitimer error, can't start timer"
				<< std::endl;
		return -1;
	}

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
int uthread_spawn(void (*f)(void)) {
	sigset_t set;
	blockAlarmSignal(set);

	try {
		for (int index = 0; index < MAX_THREAD_NUM; index++) {
			if (threads[index] == nullptr) {
				//create the new thread
				UThread* newThread = new UThread(index, f);
				threads[index] = newThread;
				readyId.push_back(index);
				unBlockAlarmSignal(set);
				return index;
			}
		}
	} catch (...) {
		std::cerr
				<< "system error: uthread_spawn error, can't create new thread"
				<< std::endl;
		unBlockAlarmSignal(set);
		return -1;
	}

	std::cerr << "system error: uthread_spawn error, can't create new thread"
			<< std::endl;
	//unblocked signals: SIGVTALRM
	unBlockAlarmSignal(set);
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
int uthread_terminate(int tid) {
	sigset_t set;
	//blocked signals: SIGVTALRM
	blockAlarmSignal(set);

	if (threads[tid] == nullptr) {
		unBlockAlarmSignal(set);
		std::cerr
				<< "thread library error: can't terminate non existing thread"
				<< std::endl;
		return -1;
	} else if (tid == 0) {
		deleteAllThreads();
		exit(0);
	} else if (threads[tid]->state == RUNNING) {
		delete threads[tid];
		threads[tid] = nullptr;
		unBlockAlarmSignal(set);
		switchThreads(SIGVTALRM);
		return 0;
	}
	deleteThread(tid);

	//unblocked signals: SIGVTALRM
	unBlockAlarmSignal(set);

	return 0;
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED or SLEEPING states has no
 * effect and is not considered as an error.
 * Return value: On success, return 0. On failure, return -1.
 */
int uthread_block(int tid) {
	sigset_t set;
	//blocked signals: SIGVTALRM
	blockAlarmSignal(set);

	if (tid == 0) {
		std::cerr << "thread library error: can't block main thread"
				<< std::endl;
		unBlockAlarmSignal(set);
		return -1;
	}
	else if (threads[tid] == nullptr) {
		std::cerr << "thread library error: can't block non existing thread"
				<< std::endl;
		unBlockAlarmSignal(set);
		return -1;
	}
	else if(threads[tid]->state == BLOCKED || threads[tid]->state == SLEEPING){
		unBlockAlarmSignal(set);
		return 0;
	}
	else if (tid == runningId) {
		blockId.push_back(tid);
		threads[tid]->state = BLOCKED;
		int ret = sigsetjmp(threads[runningId]->env, 1);
		if (ret == 1) {
			//unBlockSIGVALM(set);
			return 0;
		}

		unBlockAlarmSignal(set);
		switchThreads(SIGVTALRM);
		return 0;
	}
	eraseFromList(readyId, tid);
	blockId.push_back(tid);
	threads[tid]->state = BLOCKED;

	//unblocked signals: SIGVTALRM
	unBlockAlarmSignal(set);
	return 0;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state. Resuming a thread in the RUNNING, READY or SLEEPING
 * state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered as an error.
 * Return value: On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid) {
	sigset_t set;
	//blocked signals: SIGVTALRM
	blockAlarmSignal(set);

	if (threads[tid] == nullptr) {
		unBlockAlarmSignal(set);
		std::cerr << "thread library error: can't resume not existing thread"
				<< std::endl;
		return -1;
	}
	if (threads[tid]->state == BLOCKED) {
		eraseFromList(blockId, tid);
		readyId.push_back(tid);
		threads[tid]->state = READY;
	}

	//unblocked signals: SIGVTALRM
	unBlockAlarmSignal(set);

	return 0;
}

/*
 * Description: This function puts the RUNNING thread to sleep for a period
 * of num_quantums (not including the current quantum) after which it is moved
 * to the READY state. num_quantums must be a positive number. It is an error
 * to try to put the main thread (tid==0) to sleep. Immediately after a thread
 * transitions to the SLEEPING state a scheduling decision should be made.
 * Return value: On success, return 0. On failure, return -1.
 */
int uthread_sleep(int num_quantums) {
	sigset_t set;
	blockAlarmSignal(set);
	if (num_quantums < 0) {
		std::cerr << "thread library error: negative number of sleep quantums"
				<< std::endl;
		unBlockAlarmSignal(set);
		return -1;
	}

	if (runningId == 0) {
		std::cerr
				<< "thread library error: it's illegal "
				<< "to put the main thread to sleep"
				<< std::endl;

		unBlockAlarmSignal(set);
		return -1;
	}
	sleepId.push_back(runningId);
	threads[runningId]->state = SLEEPING;
	threads[runningId]->remainingSleepQuantum = num_quantums;

	unBlockAlarmSignal(set);
	switchThreads(SIGVTALRM);
	return 0;
}

/*
 * Description: This function returns the number of quantums until the thread
 * with id tid wakes up including the current quantum. If no thread with ID
 * tid exists it is considered as an error. If the thread is not sleeping,
 * the function should return 0.
 * Return value: Number of quantums (including current quantum) until wakeup.
 */
int uthread_get_time_until_wakeup(int tid) {
	if (threads[tid] != nullptr) {
		return threads[tid]->remainingSleepQuantum;
	}
	std::cerr << "thread library error: thread not found" << std::endl;
	return -1;
}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
 */
int uthread_get_tid() {
	return runningId;
}

/*
 * Description: This function returns the total number of quantums that were
 * started since the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
 */
int uthread_get_total_quantums() {
	return totalQuantums;
}

/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered as an error.
 * Return value: On success, return the number of quantums of the thread with
 *  ID tid. On failure, return -1.
 */
int uthread_get_quantums(int tid) {
	if (threads[tid] != nullptr) {
		return threads[tid]->runningTime;
	}
	std::cerr << "thread library error: thread not found" << std::endl;
	return -1;
}

