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
	//��ô���run()�����ķ���ֵ�����Ա�ʾ��������
	//C++17 Any����
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
	//���⣺ThreadPool���������Ժ���ô���̳߳���ص���Դȫ������
	{
		ThreadPool pool;
		//�û��Լ������̳߳صĹ���ģʽ
		pool.setMode(PoolMode::MODE_CACHE);
		pool.start(4);
		//������������Result����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		uLong sum1 = res1.get().cast_<uLong>();
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		//Master -Slave�߳�ģ��
		//Master�߳������ֽ�����Ȼ�������Slave�̷߳�������
		//�ȴ�����Slave�߳�ִ�������񣬷��ؽ��
		//Master�̺߳ϲ����������������
		std::cout << sum1 + sum2 + sum3 << std::endl;
	}
	//res.get();//get����Any���ͣ���ôת�ɾ��������
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