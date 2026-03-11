#ifndef __DB_OPS_H
#define __DB_OPS_H

#include <sqlite3.h>    // sqlite数据库
#include <stdio.h>      // 标准输入输出函数
#include <stdlib.h>     // 标准库函数
#include <string.h>

#define CHATMESSAGESQLITE_PATH     "data/chatMessage.db"        // 聊天记录数据库路径
#define USERINFOSQLITE_PATH     "data/userInfo.db"       // 客户端连接服务器路径

sqlite3* sqliteInit_chatMessageDB();       // 初始化sqlite聊天记录数据库
sqlite3* sqliteInit_userInfoDB();         // 初始化sqlite用户数据的数据库
void upsert_cilent(sqlite3* db,const char* account,const char* ip,int port);    // 插入或者更新客户端信息
int insert_messagee(sqlite3* db,const char* sender,const char* receiver,const char* message);  // 插入聊天记录
int query_cilent_info(sqlite3* db,const char* account,char* ip,int ip_size,int* port_out);        // 查询账号所对应的ip和端口

#endif