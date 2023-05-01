/*
    数据库连接池，使用单例模式保证唯一
*/
#pragma once

#include <stdio.h>
#include <string>
#include <list>
#include <iostream>
#include <mysql.h>
#include <mutex>
#include "../lock/Locker.hpp"

// namespace sql_conn_ns
// {
class ConnectionPool
{
private:
    static ConnectionPool *_pool;
    static std::mutex *_mtx; // 获取单例时需加锁保护

public:
    std::string _host;    // 主机地址
    std::string _port;    // 数据库端口
    std::string _user;    // 数据库用户名
    std::string _passwd;  // 数据库密码
    std::string _db_name; // 数据库名
    int _enable_log;      // 日志开关

private:
    int _max_conn_nums;  // 最大连接数
    int _cur_conn_nums;  // 当前连接数
    int _free_conn_nums; // 空闲连接数
    locker_ns::Locker _lock;
    locker_ns::Sem _sem;
    std::list<MYSQL *> _conn_list; // 连接池

public:
    void init(const std::string &host,
              const std::string &user,
              const std::string &password,
              const std::string &db_name,
              int port,
              int max_conn_nums,
              int enable_log){};
    static ConnectionPool *get_instance(){}; // 获取单例
    MYSQL *get_conn(){};                     // 获取数据库连接
    bool release_conn(MYSQL *conn){};        // 释放连接
    int get_free_conn_nums(){};              // 获取空闲连接
    void destory_pool(){};                   // 清理连接池

private:
    ConnectionPool(){}
    ~ConnectionPool(){}
    ConnectionPool(const ConnectionPool &) = delete;            // 禁止拷贝
    ConnectionPool &operator=(const ConnectionPool &) = delete; // 禁止赋值
};
// static成员变量需在类外初始化
ConnectionPool *ConnectionPool::_pool = nullptr;
std::mutex *ConnectionPool::_mtx = new std::mutex();

// 创建对象时从连接池中取出一个Mysql连接，对象生命周期结束时调用析构函数将Mysql连接归还连接池
class ConnectionPoolRAII
{
private:
    MYSQL *_sql_conn;
    ConnectionPool *_conn_pool;

public:
    ConnectionPoolRAII(ConnectionPool *, MYSQL **){}
    ~ConnectionPoolRAII(){}
};
