#ifndef _TIMER_H
#define _TIMER_H

#include <sys/time.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

extern "C" {
#include "intrusiveList.h"
}

using namespace std;

#define loopus 200000
#define TotalBucket 60

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)



/**
 * @brief 秒级定时器
 */
template <class Owner, class Arg>
class SafeTimer {

public:
	// 超时回调函数
	typedef int32_t (*timeoutCallback)(Owner owner, Arg arg);

	struct timerEvent {
		/**
		 * @brief 链表结点，用于加入超时链表
		 */
		list_head_t timerNode;
		/**
		 * @brief 回调函数
		 */
		timeoutCallback func;
		/**
		 * @brief 超时时间戳,>ExpireTime将触发回调函数
		 */
		time_t expireTime;
		/**
		 * @brief 回调函数作用对象
		 */
		Owner owner;
		/**
		 * @brief 回调函数其他参数
		 */
		Arg arg;

		timerEvent() {
//			cout << "timerEvent create:" << time(NULL) << endl;
		}

		 ~timerEvent() {
//			cout << "timerEvent delete:" << time(NULL) << endl;
		 }
	};

public:
	int32_t init();
	int32_t unInit();

	int32_t addEvent(timeoutCallback func, time_t expireTime, const Owner &owner, const Arg &arg);
	int32_t checkEvent();
	
	void initCurBucket();
	void fixCurBucket();
	static void *timerLoopProc(void *arg);

private:
	int32_t delEvent(timerEvent *event);

private:
	list_head_t timerHead[TotalBucket];//60秒时间桶
	mutable list_head_t *curBucket;
	mutable pthread_mutex_t m_mutex;
	mutable uint32_t curTm;            //当前时间
	mutable uint32_t secStep;          //已处理的时间step

#define LOCK   pthread_mutex_lock(&m_mutex)
#define UNLOCK pthread_mutex_unlock(&m_mutex)
};



template <class Owner, class Arg>
int32_t SafeTimer<Owner,Arg>::init()
{
	initCurBucket();

	pthread_t tid;
	int32_t ret = pthread_create(&tid, NULL, timerLoopProc, this);
	if (ret != 0) {
		return -1;
	}

	pthread_mutexattr_t attr;
	ret = pthread_mutexattr_init(&attr);
	if (ret != 0) {
		return -1;
	}

	ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (ret != 0) {
		return -1;
	}

	ret = pthread_mutex_init(&m_mutex, &attr);
	if (ret != 0) {
		return -1;
	}

	return 0;
}


template <typename Owner, typename Arg>
int32_t SafeTimer<Owner,Arg>::unInit()
{
	LOCK;

	for (size_t i = 0; i < TotalBucket; i++) {
		curBucket = &(timerHead[i]);
		list_head_t *pos, *next;
		list_for_each_safe(pos, next, curBucket) {
			delEvent(reinterpret_cast<timerEvent *>(pos));
		}

		INIT_LIST_HEAD(curBucket);
	}

	UNLOCK;
	pthread_mutex_destroy(&m_mutex);
	return 0;
}


template <typename Owner, typename Arg>
int32_t SafeTimer<Owner,Arg>::addEvent(timeoutCallback func, time_t expireTime, const Owner &owner, const Arg &arg)
{
	LOCK;

	timerEvent *event = new timerEvent;
	if (unlikely(!event)) return -1;

    event->expireTime = expireTime;
    event->func       = func;
    event->owner      = owner;
    event->arg        = arg;

	list_head_t *pos, *next;
	list_for_each_safe(pos, next, curBucket) {
		timerEvent *e = reinterpret_cast<timerEvent *>(pos);
		if (e->expireTime > expireTime) {
			list_add_tail(&event->timerNode, pos);
			UNLOCK;
			return 0;
		}
	}

    list_add_tail(&event->timerNode, curBucket);

	UNLOCK;
    return 0;
}


template <typename Owner, typename Arg>
int32_t SafeTimer<Owner,Arg>::delEvent(timerEvent *event)
{
    if (unlikely(event == NULL)) return -1; 

    list_del_init(&event->timerNode);
    event->expireTime = 0;
    event->func       = NULL;

	delete event;
	event = NULL;

    return 0;
}


template <typename Owner, typename Arg>
int32_t SafeTimer<Owner,Arg>::checkEvent()
{
	LOCK;

    list_head_t *pos, *next;
    list_for_each_safe(pos, next, curBucket) {
        timerEvent *e = reinterpret_cast<timerEvent *>(pos);
        if (e->expireTime > curTm) {
            break;
        }

        if (likely(e->func)) {
            e->func(e->owner, e->arg);
        }

		delEvent(e);
    }

	UNLOCK;
    return 0;
}


template <typename Owner, typename Arg>
void SafeTimer<Owner,Arg>::initCurBucket()
{
	for (size_t i = 0; i < TotalBucket; i++) {
		curBucket = &(timerHead[i]);
		INIT_LIST_HEAD(curBucket);
	}

	timeval tv;
	gettimeofday(&tv, 0);

	secStep   = tv.tv_sec;
	curBucket = &(timerHead[secStep % TotalBucket]);
}


template <typename Owner, typename Arg>
void SafeTimer<Owner,Arg>::fixCurBucket()
{
	static timeval tv;
	static uint32_t secStep;

	gettimeofday(&tv, 0);
	curTm = tv.tv_sec;

	LOCK;

	/**
	 *  这里负责整个定时器的secStep追赶机制
	 *
	 *  产生原因：调用func耗时过长，会导致curTm的前后差距到几秒以上
	 *
	 *  这里处理方式：发现当前时间戳（curTm）大于我们桶指针（curBucket）正在处理的时间桶（secStep）
	 *                我们将时间桶加1，保证按顺序时间桶都会遍历到。
	 *
	 *  需要配合的：func应该注意不要放很重很占用时间的逻辑
	 */
	if (curTm > secStep) {
		secStep ++;
		curBucket = &(timerHead[secStep % TotalBucket]);
	}

	UNLOCK;
}


template <typename Owner, typename Arg>
void *SafeTimer<Owner,Arg>::timerLoopProc(void *arg)
{
	for ( ; ; ) {
		usleep(loopus);
		(reinterpret_cast<SafeTimer *>(arg))->checkEvent();
		(reinterpret_cast<SafeTimer *>(arg))->fixCurBucket();
	}
	pthread_exit(0);
}

#endif 
