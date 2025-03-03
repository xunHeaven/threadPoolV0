#include<iostream>
#include <chrono>
#include <thread>
#include "threadpool.h"

using uLong = unsigned long long;
class MyTask : public Task
{
public:
	MyTask(int begin,int end)
		:begin_(begin)
		,end_(end)
	{}
	//怎么设计run()函数的返回值，可以表示任意类型
	//C++17 Any类型
	Any run()
	{
		std::cout << " tid:" << std::this_thread::get_id()
			<<"begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << " tid:" << std::this_thread::get_id()
			<< "end!" << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};
int main()
{
	//问题：ThreadPool对象析构以后，怎么把线程池相关的资源全部回收
	{
		ThreadPool pool;
		//用户自己设置线程池的工作模式
		pool.setMode(PoolMode::MODE_CACHE);
		pool.start(4);
		//如何设置这里的Result机制
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		uLong sum1 = res1.get().cast_<uLong>();
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		//Master -Slave线程模型
		//Master线程用来分解任务，然后给各个Slave线程分配任务
		//等待各个Slave线程执行完任务，返回结果
		//Master线程合并各个任务结果，输出
		std::cout << sum1 + sum2 + sum3 << std::endl;
	}
	//res.get();//get返回Any类型，怎么转成具体的类型
	/*int sum = res.get().cast_<int>();
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());
	pool.submitTask(std::make_shared<MyTask>());*/
	getchar();
	//std::this_thread::sleep_for(std::chrono::seconds(5));
	return 0;
}