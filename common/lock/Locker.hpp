#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
/*
    线程同步机制封装类
    分别对信号量，锁，条件变量进行封装
*/

namespace locker_ns{
    class Locker{
    private:
        pthread_mutex_t _mtx;
    public:
        Locker(){
            if(pthread_mutex_init(&_mtx,nullptr) != 0){
                throw std::exception();
            }
        }
        ~Locker(){
            pthread_mutex_destroy(&_mtx);
        }

        void lock(){
            pthread_mutex_lock(&_mtx);
        }

        void unlock(){
            pthread_mutex_unlock(&_mtx);
        }

        pthread_mutex_t* get(){
            return &_mtx;
        }
    };

    /*
        对条件变量封装
    */
    class Cond{
    private:
        pthread_cond_t _cond;
    public:
        Cond(){
            if(pthread_cond_init(&_cond,nullptr)){
                throw std::exception();
            }
        }
        ~Cond(){
            pthread_cond_destroy(&_cond);
        }

        void wait(pthread_mutex_t *mtx){
            pthread_cond_wait(&_cond,mtx);
        }

        void signal(){
            pthread_cond_signal(&_cond);
        }

        void time_wait(){

        }

        void broad_cast(){

        }
    };

    class Sem{
    private:
        sem_t _sem;
    public:
        Sem(int num = 0){
            if(sem_init(&_sem,0,num) != 0){
                throw std::exception();
            }
        }
        ~Sem(){
            sem_destroy(&_sem);
        }
        // 申请得到一个信号量
        bool wait(){
            return sem_wait(&_sem) == 0;
        }
        // 释放信号量
        bool post(){
            return sem_post(&_sem) == 0;
        }
    };
}
#endif