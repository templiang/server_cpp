#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <time.h>

namespace timer_ns
{
    class Timer;
    struct ClientInfo
    {
        struct sockaddr_in _address;
        int _sockfd;
        Timer *_timer;
    };
    typedef void (*callback_t)(ClientInfo *);

    // 定时器类
    struct Timer
    {
        time_t _expire_time;    // 超时时间
        callback_t _cb_func;    // 回调函数
        ClientInfo *_user_info; // 链接资源信息
        Timer *_pre;
        Timer *_next;

        Timer() : _cb_func(nullptr), _user_info(nullptr), _pre(nullptr), _next(nullptr)
        {
        }
    };

    class SortTimerList
    {
    private:
        Timer *_head;
        Timer *_tail;

    private:
        void add_timer(Timer *timer, Timer *list_head){};

    public:
        SortTimerList() : _head(nullptr), _tail(nullptr)
        {
        }
        ~SortTimerList()
        {
            Timer *tmp = _head;
            while (tmp)
            {
                _head = tmp->_next;
                delete tmp;
                tmp = _head;
            }
        }

        void add_timer(Timer *timer);    // 像定时器表中新加入一个定时器
        void adjust_timer(Timer *timer); // 调整定时器，任务发生变化时，调整定时器在链表中的位置
        void del_timer(Timer *timer);
        void tick();
    };

    class Utils
    {
    public:
        static int *_user_pipefd;
        SortTimerList _timer_list;
        static int _epfd;
        int _time_slot;

    public:
        Utils() {}
        ~Utils() {}

        void init(int fd){};
        static void sig_handler(int sig){};
        void set_sig(int sig, void (*handler)(int), bool restart = true){};

        // 定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler(){};
        void show_error(int conn_fd, const char *info){};
    };

    void SortTimerList::add_timer(Timer *timer)
    {
        // 1.timer为空指针
        if (timer == nullptr)
        {
            return;
        }
        // 2.list中无结点
        if (_head == nullptr)
        {
            _head = _tail = timer;
            return;
        }

        // 3.list表中已有结点，list按超时时间参数升序排列，找到响应的插入位置。
        Timer *tmp = _head;
        while (_head != nullptr)
        {
            if (timer->_expire_time < tmp->_expire_time)
            {
                break;
            }
            tmp = tmp->_next;
        }
        // 头插
        if (tmp == _head)
        {
            timer->_next = _head;
            _head->_pre = timer;
            _head = timer;
        }
        // 尾插
        else if (tmp == nullptr)
        {
            _tail->_next = timer;
            timer->_pre = _tail;
            _tail = timer;
        }
        // 中间插入
        else
        {
            Timer *pre = tmp->_pre;
            pre->_next = timer;
            timer->_pre = pre;
            timer->_next = tmp;
            tmp->_pre = timer;
        }
    }

    void SortTimerList::adjust_timer(Timer *timer)
    {
        if (timer == nullptr)
        {
            return;
        }
        Timer *tmp = timer->_next;
        if (!tmp || (timer->_expire_time < tmp->_expire_time))
        {
            return;
        }
        if (timer == _head)
        {
            _head = _head->_next;
            _head->_pre = NULL;
            timer->_next = NULL;
            add_timer(timer);
        }
        else
        {
            timer->_pre->_next = timer->_next;
            timer->_next->_pre = timer->_pre;

            // 此处似乎有问题,待调试
            add_timer(timer);
        }
    }

    void SortTimerList::del_timer(Timer *timer)
    {
        if (timer == nullptr)
        {
            return;
        }
        // 链表中仅有一个timer结点
        if ((timer == _head) && (timer == _tail))
        {
            delete timer;
            _head = nullptr;
            _tail = nullptr;
            return;
        }
        // timer为头节点
        if (timer == _head)
        {
            _head = _head->_next;
            _head->_pre = nullptr;
            delete timer;
            return;
        }
        // timer为尾结点
        else if (timer == _tail)
        {
            _tail = _tail->_pre;
            _tail->_next = nullptr;
            delete timer;
            return;
        }
        // timer为中间结点
        timer->_pre->_next = timer->_next;
        timer->_next->_pre = timer->_pre;
        delete timer;
    }
    
    // 定时任务处理函数。当SIGALRM信号被触发时，主循环中调用一次定时任务处理函数，处理链表容器中到期的定时器
    void SortTimerList::tick()
    {
        if (_head == nullptr)
        {
            return;
        }

        time_t cur_time = time(nullptr);
        Timer *tmp = _head;
        
        // 判断结点是否到期，到期则通过回调函数处理之
        while (tmp)
        {
            if (cur_time < tmp->_expire_time){
                
            }
        }
    }
}
