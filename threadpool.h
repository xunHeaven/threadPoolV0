//#pragma once //windows ��������ֹͷ�ļ��ظ�������
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
//Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//������캯��������Any���ͽ�����������������
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}//base_�����еĴ�����new Derive<T>(data)�ȼ�

	//��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		//��ô��base_�ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
		//����ָ��ת��Ϊ������ָ�� RTTI����ʶ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base
	{
	public:
		virtual ~Base()=default;
	};
	//����������
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data):data_(data)
		{}
		T data_;//������������������
	};
private:
	//����һ������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit=0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;
	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}
	//����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;//��Դ����
	std::mutex mtx_;
	std::condition_variable cond_;
};

//Task����ǰ������
class Task;

//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	//����һ��setVal������ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	//�������get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_;//�洢����ķ���ֵ
	Semaphore sem_;//�߳�ͨ�ŵ��ź���
	std::shared_ptr<Task> task_;//ָ���Ӧ��ȡ����ֵ��������� 
	std::atomic_bool isValid_;//����ֵ�Ƿ���Ч
};

//����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	//�û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result* result_;//Result������������ڴ���Task
};

//�̳߳�֧������ģʽfix��cache
//��class���Ա���ö�����Ͳ�ͬ����ö������ͬ�ĳ�ͻ
enum class PoolMode
{
	MODE_FIXED,//�̶��������߳�
	MODE_CACHE,//�߳������ɶ�̬����
};

//�߳�������Ҫ����
class Thread
{
public:
	//ʹ��using�ؼ���������һ�����ͱ�����
	// ������˵����������һ����ΪThreadFunc�����ͱ������������������std::function<void()>���͡�
	//std::function<void()>������C++��׼���е�һ��ͨ�ú�����װ����
	// �����Դ洢�����ƺ͵����κο��Խ���(�޲���)��������Ϊint������void�ĺ�������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳���
	Thread(ThreadFunc func);
	// �߳�����
	~Thread();
	//�����߳�
	void start();
	
	//��ȡ�߳�id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�id
};

//�̳߳�������Ҫ�������
class ThreadPool
{
public:
	ThreadPool();//�̳߳ع���
	~ThreadPool();//�̳߳�����
	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode); 
	//���ó�ʼ���߳�����,����ֱ�Ӳ��뵽start()
	//void setInitThreadSize(int size);
	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);
	//����cacheģʽ�£��̳߳ص��߳�������ֵ
	void setThreadSizeThreshHold(int threshhold);
	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);
	//�����̳߳�
	void start(int initThreadSize=4);

	ThreadPool(const ThreadPool&) = delete;//ɾ����ThreadPool��ĸ��ƹ��캯����
	//ɾ����ThreadPool��ĸ��Ƹ�ֵ������
	ThreadPool& operator=(const ThreadPool&)=delete;
private:
	//�����̺߳���
	void threadFunc(int threadid);
	//���pool������״̬
	bool checkRunningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
	int initThreadSize_;//��ʼ�߳�����
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳����̵߳�������
	std::atomic_int idleThreadSize_;//��¼�����̵߳�����
	int threadSizeThreshHold_;//�߳�����������ֵ

	std::queue<std::shared_ptr<Task>> taskQue_;//������У�����ָ�룬Ϊ���ӳ�����ĵ���������
	//�ڶ��̻߳����У��������߳���Ҫ��ȡ���޸� taskSize_ ��ֵ��ʹ�� std::atomic_int ���Ա�֤��Щ������ԭ�ӵģ��Ӷ�����Ǳ�ڵ����ݾ����Ͳ�ȷ������Ϊ��
	std::atomic_int taskSize_;//���������
	int taskQueMaxThreshHold_;//�����������������������ֵ

	//�û��̺߳��̳߳��ڲ��̶߳����������в���Ӱ�죬������Ҫ�̻߳�������֤������е��̰߳�ȫ
	std::mutex taskQueMtx_;
	std::condition_variable notFull_;//��ʾ������в��������û��߳̿������������������
	std::condition_variable notEmpty_;//��ʾ������в��գ������Դ����������ȡ������ִ��
	std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

	PoolMode poolMode_;//��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_;//��ʾ��ǰ�̳߳ص�����״̬
	
};

#endif // ! THREADPOOL_H

