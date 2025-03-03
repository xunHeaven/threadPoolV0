#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include<chrono>
const int TASK_MAX_THRESHHOLD = 1024;
const int Thread_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;//��λ����
//�̳߳ع���
//���ֳ�ʼ����ʽ���ڹ��캯������ʹ�ø�ֵ������Ч����Ϊ��ֱ���ڹ�������ʱ���ʼ����Ա�������������ȹ����ٸ�ֵ��
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(Thread_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRunning_(false)
{ }//{ } ��ʾ���캯���ĺ�����Ϊ�գ�û��ִ���κ���������

//�̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	notEmpty_.notify_all();
	//�ȴ��̳߳������е��̷߳��� �����е��߳�������״̬��������ָ��ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

//����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

//����cacheģʽ�£��̳߳ص��߳�������ֵ
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

//���̳߳��ύ����,�û����øýӿڴ������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�̵߳�ͨ�ţ��ȴ���������п���
	/* while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}*/
	//�������д����while�����д���ȼ�
	//notFull_.wait(lock, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; });
	//�û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() <(size_t) taskQueMaxThreshHold_; }))
	{
		//��ʾnotFull_�ȴ�1s��������Ȼû������
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp,false);//����
	}
	
	//����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��Ϊ�·�������������в��գ�notEmpty_�Ͻ���֪ͨ,���Է����߳�ִ������
	notEmpty_.notify_all();

	//cacheģʽ��������ȽϽ�����������С���������
	//��Ҫ�������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHE
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ <= threadSizeThreshHold_)
	{
		std::cout << ">>>>create new thread!"<< std::endl;
		//�������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		threads_[threadId]->start();//�����߳�
		
		curThreadSize_++;//�޸ĵ�ǰ�߳�����
		idleThreadSize_++;//�޸Ŀ����߳�����
	 }

	//���������Result����
	return  Result(sp);//һ��task->getResult(),result����task������ﲻ����
	//����Result(task),task������Result��
}
//�����̳߳�
void ThreadPool::start(int initThreadSize)
{
	//�����̳߳ص�����״̬
	isPoolRunning_ = true;
	//��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_= initThreadSize;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		//����thread�̶߳����ʱ�򣬰��̺߳���threadFunc����thread�̶߳���
		//threads_.emplace_back(new Thread());
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(ptr);//��һ�лᱨ��
		// ����Ϊunique_ptr���ص���ֻ����һ��ָ��ָ������ڴ�,�����������ͨ�Ŀ�������͸�ֵ
		//threads_.emplace_back(std::move(ptr));
	}

	//���������߳�,std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();//��Ҫִ���̺߳���
		idleThreadSize_++;//��¼��ʼ�����߳�����
	}
}

//�����̺߳������̳߳ص������̴߳������������������
void ThreadPool:: threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	while(isPoolRunning_)
	{
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << " tid:" << std::this_thread::get_id()
				<< "���Ի�ȡ���񡣡���" << std::endl;

			//��cacheģʽ���п����Ѿ������˺ܶ��̣߳����ǿ���ʱ�䳬��60s��
			// Ӧ�ðѶ����̻߳��յ�(����initThreadSize_�������߳�Ҫ�����յ�)
			//��ǰʱ��-��һ���߳�ִ�е�ʱ��>60s
			while (taskQue_.size() == 0)
			{
				//ÿ�뷵��һ��  ��ô���֣���ʱ���أ������������ִ�з���
				if (poolMode_ == PoolMode::MODE_CACHE)
				{
					//����������ʱ������
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&&curThreadSize_>initThreadSize_)
						{
							//��ʼ���յ�ǰ�߳�
							//��¼�̱߳�������ر�����ֵ���޸�
							//���̶߳�����߳��б�������ɾ��
							//threadid=>thread����=>ɾ��
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
					//�ȴ�notEmpty_
					//notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0; });
					notEmpty_.wait(lock);
				}
				//�̳߳ؽ�����Ҫ�����߳���Դ
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
			<< "��ȡ����ɹ�������" << std::endl;
			//�����������ȡ��һ������
			task = taskQue_.front();//�Ӷ���ͷȡ������
			taskQue_.pop();//��ȡ������������������ɾ��
			taskSize_--;

			//ȡ��һ��������������л�������֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//ȡ��һ�����񣬽���֪ͨ,���в���,�û��߳̿����ύ����
			notFull_.notify_all();
		}//ȡ��һ������͸ð����ͷŵ�
		

		 //��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			//task->run();//1.ִ������2.������ķ���ֵͨ��setVal��������Result
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();//�����߳�ִ���������ʱ��

	}
	threads_.erase(threadid);
	std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
	exitCond_.notify_all();
}
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}
/////////�̷߳�����ʵ��

//��̬��Ա������Ҫ�������ʼ��
int Thread::generateId_ = 0;
//�̹߳���
Thread::Thread(ThreadFunc func) 
	:func_(func)//�̺߳������յ��ǰ���bind�а󶨵�threadFunc����
	,threadId_(generateId_++)
{}
// �߳�����
Thread::~Thread() {}

//�����߳�
void Thread::start()
{
	//����һ���߳���ִ���̺߳���
	//������һ���µ��߳� t����������������ʼִ�д��ݸ����ĺ��� func_��
	std::thread t(func_,threadId_);
	//���д��뽫�´������߳� t ���루detach��������һ���̱߳����룬���ͻ����ִ�У�����ԭ���� std::thread ���������κι���
	t.detach();

}

//��ȡ�߳�id
int Thread::getId() const
{
	return threadId_;
}




///////////////Task����ʵ��
Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());//������̬����
	}
	
}
void Task::setResult(Result* res)
{
	result_ = res;
}

///////////Result������ʵ��
Result::Result(std::shared_ptr<Task> task,bool isValid)
	:isValid_(isValid)
	,task_(task)
{
	task_->setResult(this);
}

Any Result::get()//�û����õ�
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();//task�������û��ִ���꣬����������û��߳�
	return std::move(any_);
}

void Result::setVal(Any any)
{
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();
}
