#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include<chrono>
const int TASK_MAX_THRESHHOLD = 1024;
const int Thread_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;//单位：秒
//线程池构造
//这种初始化方式比在构造函数体内使用赋值语句更高效，因为它直接在构造对象的时候初始化成员变量，而不是先构造再赋值。
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(Thread_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRunning_(false)
{ }//{ } 表示构造函数的函数体为空，没有执行任何其他操作

//线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	notEmpty_.notify_all();
	//等待线程池里所有的线程返回 ，池中的线程有两种状态：阻塞和指针执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

//设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

//设置cache模式下，线程池的线程上限阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHE)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

//给线程池提交任务,用户调用该接口传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程的通信，等待任务队列有空余
	/* while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}*/
	//下面这行代码和while的两行代码等价
	//notFull_.wait(lock, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; });
	//用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() <(size_t) taskQueMaxThreshHold_; }))
	{
		//表示notFull_等待1s，条件仍然没有满足
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp,false);//错误
	}
	
	//如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//因为新放了任务，任务队列不空，notEmpty_上进行通知,可以分配线程执行任务
	notEmpty_.notify_all();

	//cache模式，任务处理比较紧急，场景：小而快的任务
	//需要根据任务数量和空闲线程数量，判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHE
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ <= threadSizeThreshHold_)
	{
		std::cout << ">>>>create new thread!"<< std::endl;
		//创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		threads_[threadId]->start();//启动线程
		
		curThreadSize_++;//修改当前线程数量
		idleThreadSize_++;//修改空闲线程数量
	 }

	//返回任务的Result对象
	return  Result(sp);//一、task->getResult(),result套在task里，在这里不可以
	//二、Result(task),task隶属于Result里
}
//开启线程池
void ThreadPool::start(int initThreadSize)
{
	//设置线程池的运行状态
	isPoolRunning_ = true;
	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_= initThreadSize;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		//创建thread线程对象的时候，把线程函数threadFunc给到thread线程对象
		//threads_.emplace_back(new Thread());
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(ptr);//这一行会报错，
		// 是因为unique_ptr的特点是只允许一个指针指向这块内存,不允许进行普通的拷贝构造和赋值
		//threads_.emplace_back(std::move(ptr));
	}

	//启动所有线程,std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();//需要执行线程函数
		idleThreadSize_++;//记录初始空闲线程数量
	}
}

//定义线程函数，线程池的所有线程从任务队列里消费任务
void ThreadPool:: threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	while(isPoolRunning_)
	{
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << " tid:" << std::this_thread::get_id()
				<< "尝试获取任务。。。" << std::endl;

			//在cache模式下有可能已经创建了很多线程，但是空闲时间超过60s，
			// 应该把多余线程回收掉(超过initThreadSize_数量的线程要被回收掉)
			//当前时间-上一次线程执行的时间>60s
			while (taskQue_.size() == 0)
			{
				//每秒返回一次  怎么区分：超时返回，还是有任务待执行返回
				if (poolMode_ == PoolMode::MODE_CACHE)
				{
					//条件变量超时返回了
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&&curThreadSize_>initThreadSize_)
						{
							//开始回收当前线程
							//记录线程变量的相关变量的值的修改
							//把线程对象从线程列表容器中删除
							//threadid=>thread对象=>删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
							return;

						}
					}
				}
				else
				{
					//等待notEmpty_
					//notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0; });
					notEmpty_.wait(lock);
				}
				//线程池结束，要回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
			}
			
			
			idleThreadSize_--;

			std::cout << " tid:" << std::this_thread::get_id()
			<< "获取任务成功。。。" << std::endl;
			//从任务队列中取出一个任务
			task = taskQue_.front();//从队列头取出任务
			taskQue_.pop();//将取出的任务从任务队列中删除
			taskSize_--;

			//取出一个任务，如果队列中还有任务，通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//取出一个任务，进行通知,队列不满,用户线程可以提交任务
			notFull_.notify_all();
		}//取出一个任务就该把锁释放掉
		

		 //当前线程负责执行这个任务
		if (task != nullptr)
		{
			//task->run();//1.执行任务。2.把任务的返回值通过setVal方法给到Result
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//更新线程执行完任务的时间

	}
	threads_.erase(threadid);
	std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
	exitCond_.notify_all();
}
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}
/////////线程方法的实现

//静态成员变量需要在类外初始化
int Thread::generateId_ = 0;
//线程构造
Thread::Thread(ThreadFunc func) 
	:func_(func)//线程函数接收的是绑定器bind中绑定的threadFunc函数
	,threadId_(generateId_++)
{}
// 线程析构
Thread::~Thread() {}

//启动线程
void Thread::start()
{
	//创建一个线程来执行线程函数
	//创建了一个新的线程 t，并且它会立即开始执行传递给它的函数 func_。
	std::thread t(func_,threadId_);
	//这行代码将新创建的线程 t 分离（detach）出来。一旦线程被分离，它就会独立执行，而与原来的 std::thread 对象不再有任何关联
	t.detach();

}

//获取线程id
int Thread::getId() const
{
	return threadId_;
}




///////////////Task方法实现
Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());//发生多态调用
	}
	
}
void Task::setResult(Result* res)
{
	result_ = res;
}

///////////Result方法的实现
Result::Result(std::shared_ptr<Task> task,bool isValid)
	:isValid_(isValid)
	,task_(task)
{
	task_->setResult(this);
}

Any Result::get()//用户调用的
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();//task任务如果没有执行完，这里会阻塞用户线程
	return std::move(any_);
}

void Result::setVal(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();
}
