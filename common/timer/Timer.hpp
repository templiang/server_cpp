#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "../http/Http.hpp"
using namespace http_conn_ns;

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
        static int _epfd;
        int _timeslot;
        SortTimerList _timer_list;

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
    // 初始化静态成员变量
    int Utils::_epfd = 0;
    int *Utils::_user_pipefd = nullptr;

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
            if (cur_time < tmp->_expire_time)
            {
                break;
            }

            tmp->_cb_func(tmp->_user_info);
            _head = _head->_next;
            if (_head)
            {
                _head->_pre = nullptr;
            }
            delete tmp;
            tmp = _head;
        }
    }

    void Utils::init(int timeslot)
    {
        _timeslot = timeslot;
    }

    // 信号
    void Utils::sig_handler(int sig)
    {
        int save_errno = errno;
        int msg = sig;
        send(_user_pipefd[1], (char *)&msg, 1, 0);
        errno = save_errno;
    }

    void Utils::set_sig(int sig, void (*handler)(int), bool restart = true)
    {
        // 初始化一个sigacation结构体
        struct sigaction sa;
        memset(&sa, '\0', sizeof(sa));

        // 设置信号捕捉函数
        sa.sa_handler = handler;
        if (restart)
        {
            sa.sa_flags |= SA_RESTART;
        }
        // 将sa_mask位图中的信号设置为阻塞状态
        sigfillset(&sa.sa_mask);
        // 执行sigaction。当进程收到intsig 号信号时，调用我们自定义的信号处理函数handler
        assert(sigaction(sig, &sa, nullptr) != -1);
    }

    void Utils::timer_handler()
    {
        _timer_list.tick();
        // alarm()函数将在指定时间(s)后发送SIGALRM信号给当前进程
        alarm(_timeslot);
    }

    // 错误处理
    void Utils::show_error(int conn_fd, const char *info)
    {
        send(conn_fd, info, strlen(info), 0);
        close(conn_fd);
    }

    // 定时器对象过期后回调该函数
    void cb_func(ClientInfo *user_info)
    {
        assert(user_info); // 此处或有问题，待调试
        epoll_ctl(Utils::_epfd, EPOLL_CTL_DEL, user_info->_sockfd, 0);
        close(user_info->_sockfd);
        HttpConn::_user_count--;
    }
}
