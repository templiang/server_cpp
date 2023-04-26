/*
    这是一个线程池
*/
#pragma once

#include <pthread.h>
#include <vector>
#include <list>
#include <assert.h>
#include "../lock/Locker.hpp"

namespace thread_pool_ns
{
    template <typename T>
    class ThreadPool
    {
        typedef ThreadPool<T> *Ptr; // 指针
        typedef ThreadPool<T> Self; //
        typedef ThreadPool<T> &Ref; // 引用
    private:
        int _thread_number;                // 线程数量
        pthread_t *_threads;               // 指向线程池数组的指针
        std::vector<pthread_t> *_threads_; // todo
        int _max_requests;                 // 请求队列最大长度
        std::list<T *> _req_queue;         // 请求队列
        locker_ns::Locker _locker;         // 互斥锁，保护请求队列
        locker_ns::Sem _sem;               // 信号量，判断是否有任务需要处理.todo 使用atomic代替
        ConnectionPool *_conn_pool;        // 数据库连接池
        int _actor_model;                  // 模型

    public:
        ThreadPool(int model,
                   ConnectionPool *conn_pool,
                   int thread_number = 8,
                   int max_requests = 10000) : _actor_model(model),
                                               _conn_pool(conn_pool),
                                               _thread_number(thread_number),
                                               _max_requests(max_requests),
                                               _threads(nullptr), _threads_(nullptr),
        {
            assert(thread_number > 0 && max_requests > 0);
            _thread_number = thread_number;
            _max_requests = max_requests;
            // 申请线程池
            _threads = new pthread_t[_thread_number];
            _threads_ = new vector<pthread_t>(_thread_number);

            if (_thread == nullptr)
            {
                throw std::exception();
            }

            // 为线程池创建线程
            for (int i = 0; i < _thread_number; ++i)
            {
                // 申请线程
                if (pthread_create(&_threads[i], nullptr, worker, this) != 0)
                {
                    // 若申请线程失败，清理已经申请的资源
                    delete[] _threads;
                    throw std::exception();
                }
                // 分离线程
                if (pthread_detach(_threads[i]) != 0)
                {
                    delete[] _threads;
                    throw std::exception();
                }
            }
        }

        ~ThreadPool()
        {
            delete[] _threads;
            delete[] _threads_;
        }

        // 向请求队列中添加请求
        bool append(T *req, int state)
        {
            // 请求队列为临界资源，因此我们需要加锁
            _locker.lock();
            if (_req_queue.size() >= _max_requests)
            {
                _locker.unlock(); // 及时释放锁，谨防死锁
                return false;
            }
            req->_state = state;
            _req_queue.push_back(req);
            _locker.unlock();

            _sem.post(); // 更新信号量
            return true;
        }

        bool append_p(T *req)
        {
            _locker.lock();
            if (_req_queue.size() >= _max_requests)
            {
                _locker.unlock(); // 及时释放锁，谨防死锁
                return false;
            }
            _req_queue.push_back(req);
            _locker.unlock();

            _sem.post(); // 更新信号量
            return true;
        };

    private:
        // this指针的缘故，我们需把worker函数设置为静态成员函数
        static void *worker(void *arg);
        /*我们不希望外界能访问到run函数*/
        void run();
    };

    template <typename T>
    void *ThreadPool<T>::worker(void *arg)
    {
        // arg参数为 ThreadPool<T>对象的this指针
        ThreadPool<T> *t_pool = (ThreadPool<T> *)arg;
        t_pool->run();

        /*------- should nerver not run to here --------*/
        return t_pool;
    }

    template <typename T>
    void ThreadPool<T>::run()
    {
        while (true)
        {
            _sem.wait();
            // 此时，应有任务在请求队列。但仍有可能为空
            _locker.lock();
            if (_req_queue.empty())
            {
                _locker.unlock();
                continue;
            }

            T *req = _req_queue.front();
            _req_queue.pop_front();
            _locker.unlock();

            if (req == nullptr)
            {
                continue;
            }

            std::cout << "request run" << std::endl;
            // todo.........
        }
    }

}
