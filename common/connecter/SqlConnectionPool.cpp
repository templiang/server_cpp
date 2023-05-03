#include "SqlConnectionPool.hpp"

// static成员变量需在类外初始化
ConnectionPool *ConnectionPool::_pool = nullptr;
std::mutex *ConnectionPool::_mtx = new std::mutex();

ConnectionPool::ConnectionPool()
{
    _cur_conn_nums = 0;
    _free_conn_nums = 0;
}

ConnectionPool::~ConnectionPool()
{
    destory_pool();
}

ConnectionPool *ConnectionPool::get_instance()
{
    // 加双判定，减少锁的争用
    if (_pool == nullptr)
    {
        _mtx->lock();
        if (_pool == nullptr)
        {
            _pool = new ConnectionPool();
        }
        _mtx->unlock();
    }
    return _pool;
}

void ConnectionPool::init(const std::string &host,
                          const std::string &user,
                          const std::string &passwd,
                          const std::string &db_name,
                          int port,
                          int max_conn_nums,
                          int enable_log)
{
    _host = host;
    _port = port;
    _user = user;
    _passwd = passwd;
    _db_name = db_name;
    _enable_log = enable_log;

    // 循环创建mysql连接
    for (int i = 0; i < max_conn_nums; ++i)
    {
        MYSQL *conn = mysql_init(nullptr);

        if (conn == nullptr)
        {
            std::cout << "MYSQL ERROR" << std::endl;
            exit(1);
        }
        conn = mysql_real_connect(conn, host.c_str(),
                                  user.c_str(),
                                  passwd.c_str(),
                                  db_name.c_str(),
                                  port,
                                  nullptr, 0);

        if (conn == nullptr)
        {
            std::cout << "MYSQL ERROR" << std::endl;
            exit(1);
        }

        // 建立连接成功,更新空闲连接数
        _conn_list.push_back(conn);
        ++_free_conn_nums;
    }

    _max_conn_nums = _free_conn_nums;
    _sem = locker_ns::Sem(_max_conn_nums);
}

// 获取当前空闲连接数
int ConnectionPool::get_free_conn_nums()
{
    return _free_conn_nums;
}

MYSQL *ConnectionPool::get_conn()
{
    MYSQL *conn = nullptr;
    // 双重判断 if 与 wait()，可以减少在wait()阻塞的几率
    if (_conn_list.size() == 0)
    {
        return nullptr;
    }

    _sem.wait();
    _lock.lock();
    // 从连接池中取出一个mysql连接，并更新可用和已用连接数
    conn = _conn_list.front();
    _conn_list.pop_front();
    --_free_conn_nums;
    ++_cur_conn_nums;

    _lock.unlock();

    return conn;
}

// 将一个mysql连接还回连接池，并更新可用和已用连接数
bool ConnectionPool::release_conn(MYSQL *conn)
{
    if (conn == nullptr)
    {
        return false;
    }

    _lock.lock();

    _conn_list.push_back(conn);
    ++_free_conn_nums;
    --_cur_conn_nums;

    _lock.unlock();
    _sem.post();

    return true;
}

// 释放数据库线程池资源
void ConnectionPool::destory_pool()
{
    _lock.lock();

    for (auto iter = _conn_list.begin(); iter != _conn_list.end(); ++iter)
    {
        mysql_close(*iter);
    }

    _cur_conn_nums = 0;
    _free_conn_nums = 0;

    _lock.unlock();
}

// 创建对象时从连接池中取出一个Mysql连接，对象生命周期结束时调用析构函数将Mysql连接归还连接池
ConnectionPoolRAII::ConnectionPoolRAII(ConnectionPool *conn_pool, MYSQL **mysql)
{
    *mysql = _conn_pool->get_conn();

    _conn_pool = conn_pool;
    _sql_conn = *mysql;
}

ConnectionPoolRAII::~ConnectionPoolRAII()
{
    _conn_pool->release_conn(_sql_conn);
}
// }
