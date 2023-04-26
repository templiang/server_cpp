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

    int _listenfd;
    int _opt_linger;
    int _trig_mode;
    int _listen_trig_mode;
    int _conn_trig_mode;

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
    ClientInfo *_user_timer; // 管理定时器链表
    Utils _utils;

public:
    WebServer() {}
    ~WebServer() {}

    void init(int port, std::string user, std::string passwd, std::string dbname,
              int log_write, int opt_linger, int trig_mode, int sql_num,
              int thread_num, int enable_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void event_listen();
    void event_loop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(Timer *timer);
    void deal_timer(Timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
};

WebServer::WebServer()
{
    _http_conns = new HttpConn[MAX_FD];

    char server_path[200];
    getcwd(server_path, 200);

    _root = server_path;
    _root += "/root";

    _user_timer = new ClientInfo[MAX_FD];
}

WebServer::~WebServer()
{
    close(_epfd);
    close(_listenfd);
    close(_pipefd[1]);
    close(_pipefd[0]);
    delete[] _http_conns;
    delete[] _user_timer;
    delete _thread_pool;
}

void WebServer::init(int port, std::string user, std::string passwd, std::string dbname,
                     int log_write, int opt_linger, int trig_mode, int sql_num,
                     int thread_num, int enable_log, int actor_model)
{
    _port = port;
    _user = user;
    _passwd = passwd;
    _dbname = dbname;
    _sql_num = sql_num;
    _thread_num = thread_num;
    _log_write = log_write;
    _opt_linger = opt_linger;
    _trig_mode = trig_mode;
    _enable_log = enable_log;
    _actor_model = actor_model;
}

void WebServer::trig_mode()
{
    // LT + LT
    if (0 == _trig_mode)
    {
        _listen_trig_mode = 0;
        _conn_trig_mode = 0;
    }
    // LT + ET
    else if (1 == _trig_mode)
    {
        _listen_trig_mode = 0;
        _conn_trig_mode = 1;
    }
    // ET + LT
    else if (2 == _trig_mode)
    {
        _listen_trig_mode = 1;
        _conn_trig_mode = 0;
    }
    // ET + ET
    else if (3 == _trig_mode)
    {
        _listen_trig_mode = 1;
        _conn_trig_mode = 1;
    }
}

void WebServer::log_write()
{
    // TODO
}

void WebServer::sql_pool()
{
    // 获取连接池对象并初始化,循环建立指定数量的mysql连接
    _sql_conn_pool = ConnectionPool::get_instance();
    _sql_conn_pool->init("127.0.0.1", _user, _passwd, _dbname, 3306, _sql_num, _enable_log);

    // 将所有的username,passwd读进内存,建立user -- passwd map映射
    _http_conns->init_mysql(_sql_conn_pool);
    // _http_conns[0].init_mysql(_sql_conn_pool);这样写是否更容易理解一些？或者将_users设置为静态成员？ TODO
}

void WebServer::thread_pool()
{
    /*
        我们创建线程池对象，对象内部维护一个任务队列，该队列资源被所有线程共享。
        线程池初始化指定数量的线程，线程中运行基于任务队列的消费者函数。等待任务到来。
    */
    _thread_pool = new ThreadPool<HttpConn>(_actor_model, _sql_conn_pool, _thread_num);
}

void WebServer::event_listen()
{
    // 创建监听套接字
    _listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(_listenfd >= 0);

    // 设置套接字关闭之前，若缓冲区中仍有数据，是否需要等待数据发送完成
    if (_opt_linger == 0 || _opt_linger == 1)
    {
        struct linger tmp;
        tmp.l_onoff = _opt_linger; // 0 无需等待数据发送完成，立即关闭. 1 在关闭前等待l_linger秒
        tmp.l_linger = 1;          // 当l_onoff设置为0时，无意义
        setsockopt(_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(_port);
}
