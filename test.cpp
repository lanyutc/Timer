#include "timer.h"
#include <pthread.h>
#include <iostream>
#include <unistd.h>

using namespace std;

SafeTimer<int64_t,int64_t> timer;

int32_t print(int64_t uid, int64_t param)
{

	static timeval tv;
	gettimeofday(&tv, 0);
	cout << "out uid:" << uid << "|" << tv.tv_sec <<endl;
	return 0;
}


void *producer(void *)
{
	for (size_t i = 0; i < 100000; i++) {
		int64_t uid =  rand();
		static timeval tv;
		gettimeofday(&tv, 0);
		cout << "in uid:" << uid << "|" << tv.tv_sec <<endl;
		int32_t ret = timer.addEvent(print, time(NULL) + 1, uid, 0);
		if (ret < 0) {
			cout << "in uid error:" << endl;
		}
	}
	pthread_exit(0);
}


int main()
{
	timer.init();

	pthread_t tid;
	pthread_create(&tid, NULL, producer, NULL);
	pthread_create(&tid, NULL, producer, NULL);
	pthread_create(&tid, NULL, producer, NULL);
	pthread_create(&tid, NULL, producer, NULL);
	
	for (; ;) {
		sleep(1);
	}

	return 0;
}
