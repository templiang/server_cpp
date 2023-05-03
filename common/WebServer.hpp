/*
    WebServer类负责维护
        1.一个http连接表，包括所有的http连接对象
        2.一个http连接用户信息表，其中包含每个http连接对应的(Timer)定时器、文件描述符(fd)和struct sockaddr_in结构体
        3.所有的
*/
#pragma

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

#include "http/Http.hpp"
#include "connecter/SqlConnectionPool.hpp"
#include "threadpool/ThreadPool.hpp"
#include "timer/Timer.hpp"

using namespace http_conn_ns;
using namespace thread_pool_ns;
using namespace timer_ns;
// using namespace sql_conn_ns;

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer;

class WebServer
{
public:
    int _port;
    std::string _root;
    int _log_write;
    int _enable_log;
    int _actor_model;

    int _pipefd[2]; // 匿名管道，用于传递定时器相关
    int _epfd;
    struct epoll_event _events[MAX_EVENT_NUMBER]; // 用于保存epoll返回的就绪队列

    int _listenfd;
    int _opt_linger;
    int _trig_mode;
    TRIG_MODE _listen_trig_mode;
    TRIG_MODE _conn_trig_mode;
    // int _listen_trig_mode;
    // int _conn_trig_mode;

    HttpConn *_http_conns; //

    // 数据库
    ConnectionPool *_sql_conn_pool; // 管理数据库连接链表
    std::string _user;
    std::string _passwd;
    std::string _dbname;
    int _sql_num;

    // 线程池
    ThreadPool<HttpConn> *_thread_pool;
    int _thread_num;

    // 定时器
    timer_ns::ClientInfo *_user_timer; // 管理定时器链表
    Utils _utils;

public:
    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string passwd, std::string dbname,
              int log_write, int opt_linger, int trig_mode, int sql_num,
              int thread_num, int enable_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void event_listen();
    void event_loop();
    void http_create_with_timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(Timer *timer);
    void deal_timer(Timer *timer, int sockfd);
    bool dealclinetdata();
    bool accepter();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
};


