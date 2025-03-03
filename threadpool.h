//#pragma once //windows 中用来防止头文件重复包含的
#ifndef  THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include<memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
//Any类型，可以接收任意数据的类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//这个构造函数可以让Any类型接收任意其他的数据
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}//base_括号中的代码与new Derive<T>(data)等价

	//这个方法能把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		//怎么从base_找到他所指向的Derive对象，从它里面取出data成员变量
		//基类指针转化为派生类指针 RTTI类型识别
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//基类类型
	class Base
	{
	public:
		virtual ~Base()=default;
	};
	//派生类类型
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data):data_(data)
		{}
		T data_;//保存了任意其他类型
	};
private:
	//定义一个基类指针
	std::unique_ptr<Base> base_;
};

//实现一个信号量类
class Semaphore
{
public:
	Semaphore(int limit=0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;
	//获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源的话会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}
	//增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;//资源数量
	std::mutex mtx_;
	std::condition_variable cond_;
};

//Task类型前置声明
class Task;

//实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	//问题一、setVal方法获取任务执行完的返回值
	void setVal(Any any);
	//问题二、get方法，用户调用这个方法获取task的返回值
	Any get();
private:
	Any any_;//存储任务的返回值
	Semaphore sem_;//线程通信的信号量
	std::shared_ptr<Task> task_;//指向对应获取返回值的任务对象 
	std::atomic_bool isValid_;//返回值是否有效
};

//任务抽象基类
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	//用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;
private:
	Result* result_;//Result对象的生命周期大于Task
};

//线程池支持两种模式fix和cache
//用class可以避免枚举类型不同，但枚举项相同的冲突
enum class PoolMode
{
	MODE_FIXED,//固定数量的线程
	MODE_CACHE,//线程数量可动态增长
};

//线程类型需要抽象
class Thread
{
public:
	//使用using关键字来定义一个类型别名。
	// 具体来说，它定义了一个名为ThreadFunc的类型别名，这个别名代表了std::function<void()>类型。
	//std::function<void()>：这是C++标准库中的一个通用函数包装器，
	// 它可以存储、复制和调用任何可以接受(无参数)参数类型为int并返回void的函数对象
	using ThreadFunc = std::function<void(int)>;
	//线程构造
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	//启动线程
	void start();
	
	//获取线程id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//保存线程id
};

//线程池类型需要抽象出来
class ThreadPool
{
public:
	ThreadPool();//线程池构造
	~ThreadPool();//线程池析构
	//设置线程池的工作模式
	void setMode(PoolMode mode); 
	//设置初始的线程数量,可以直接并入到start()
	//void setInitThreadSize(int size);
	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);
	//设置cache模式下，线程池的线程上限阈值
	void setThreadSizeThreshHold(int threshhold);
	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);
	//开启线程池
	void start(int initThreadSize=4);

	ThreadPool(const ThreadPool&) = delete;//删除了ThreadPool类的复制构造函数。
	//删除了ThreadPool类的复制赋值操作符
	ThreadPool& operator=(const ThreadPool&)=delete;
private:
	//定义线程函数
	void threadFunc(int threadid);
	//检查pool的运行状态
	bool checkRunningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表
	int initThreadSize_;//初始线程数量
	std::atomic_int curThreadSize_;//记录当前线程池中线程的总数量
	std::atomic_int idleThreadSize_;//记录空闲线程的数量
	int threadSizeThreshHold_;//线程数量上限阈值

	std::queue<std::shared_ptr<Task>> taskQue_;//任务队列，智能指针，为了延长对象的的生命周期
	//在多线程环境中，如果多个线程需要读取或修改 taskSize_ 的值，使用 std::atomic_int 可以保证这些操作是原子的，从而避免潜在的数据竞争和不确定的行为。
	std::atomic_int taskSize_;//任务的数量
	int taskQueMaxThreshHold_;//任务队列中任务数量上限阈值

	//用户线程和线程池内部线程都会对任务队列产生影响，所以需要线程互斥来保证任务队列的线程安全
	std::mutex taskQueMtx_;
	std::condition_variable notFull_;//表示任务队列不满，即用户线程可以往队列中添加任务
	std::condition_variable notEmpty_;//表示任务队列不空，即可以从任务队列中取出任务执行
	std::condition_variable exitCond_;//等待线程资源全部回收

	PoolMode poolMode_;//当前线程池的工作模式
	std::atomic_bool isPoolRunning_;//表示当前线程池的启动状态
	
};

#endif // ! THREADPOOL_H

