#include "db_ops.h"

sqlite3* sqliteInit_chatMessageDB(){
    sqlite3 *chatMessageDB;
    // 打开数据库,没有数据库的话则创建数据库
    int chatMessageDBret = sqlite3_open(CHATMESSAGESQLITE_PATH,&chatMessageDB);
    if(chatMessageDBret != SQLITE_OK){
        printf("INFO::无法打开%s数据库: %s\n",CHATMESSAGESQLITE_PATH,sqlite3_errmsg(chatMessageDB));
        return NULL;
    }
    printf("INFO::成功打开%s数据库\n",CHATMESSAGESQLITE_PATH);
    // 创建聊天记录的sql语句
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "   // 自增主键
        "sender TEXT NOT NULL, "                    // 发送方账号
        "receiver TEXT NOT NULL, "                   // 接收方账号
        "message TEXT NOT NULL, "                    // 消息内容
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP" // 发送时间，默认为当前时间
        ");";
        char *err_msg = NULL;
    int rc = sqlite3_exec(chatMessageDB, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("INFO::创建表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(chatMessageDB);
        return NULL;
    }
    printf("INFO::成功创建/确认 messages 表\n");
    return chatMessageDB;
}

// 创建用户信息数据库
// 初始化sqlite用户数据的数据库
sqlite3* sqliteInit_userInfoDB(){
    sqlite3 *userInfoDB;
    // 打开数据库,没有数据库则创建数据库
    int userInfoDBret = sqlite3_open(USERINFOSQLITE_PATH,&userInfoDB);
    if(userInfoDBret != SQLITE_OK){
        printf("INFO::无法打开%s数据库: %s\n",USERINFOSQLITE_PATH,sqlite3_errmsg(userInfoDB));
        return NULL;
    }
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS clients ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "account TEXT UNIQUE NOT NULL,"
        "ip_address TEXT NOT NULL,"
        "port INTEGER NOT NULL,"
        "connect_time DATETIME DEFAULT CURRENT_TIMESTAMP);";
    
    char *err_msg = 0;
    int rc = sqlite3_exec(userInfoDB,sql,NULL,0,&err_msg);
        if (rc != SQLITE_OK) {
        printf("INFO::创建用户信息数据的数据表失败: %s\n",err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(userInfoDB);
        return NULL;
    }
    printf("INFO::成功创建用户信息表\n");
    return userInfoDB;
}

// 插入或者更新客户端信息
void upsert_cilent(sqlite3* db,const char* account,const char* ip,int port){
    // 使用参数化防止sql注入
    const char* sql =         
        "INSERT INTO clients (account, ip_address, port, connect_time) "
        "VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(account) DO UPDATE SET "           // 如果账号冲突(已存在该账号)
        "ip_address = excluded.ip_address, "            // 更新为插入时的新IP
        "port = excluded.port, "                        // 更新为插入时的新端口
        "connect_time = CURRENT_TIMESTAMP;";            // 同时更新连接时间为当前时间
    sqlite3_stmt *stmt;                                 // 声明SQL语句对象
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL); // 准备SQL语句（编译）
    if (rc != SQLITE_OK) {
        printf("INFO::准备语句失败：%s\n",sqlite3_errmsg(db));
        return;
    }
    // 绑定参数到SQL语句中的占位符（按顺序：1=account, 2=ip, 3=port）
    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_STATIC);  // 绑定账号（文本）
    sqlite3_bind_text(stmt, 2, ip, -1, SQLITE_STATIC);       // 绑定IP地址（文本）
    sqlite3_bind_int(stmt, 3, port);                          // 绑定端口号（整数）
    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {                              // SQLITE_DONE 表示执行成功
        printf("INFO::执行插入失败：%s\n",sqlite3_errmsg(db));
    } else {
        printf("INFO::客户端信息已记录: 账号=%s, IP=%s, 端口=%d\n", account, ip, port);
    }
    sqlite3_finalize(stmt);                               // 销毁SQL语句对象，释放资源
}

// 插入聊天记录
int insert_messagee(sqlite3* db,const char* sender,const char* receiver,const char* message){
    if(db == NULL || sender == NULL || receiver == NULL || message == NULL){
        printf("INFO::插入消息失败: 参数不能为空\n");
        return -1;
    }
    // SQL 插入语句（使用参数占位符 ? 防止 SQL 注入）
    const char *sql = "INSERT INTO messages (sender, receiver, message) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = NULL;

    // 准备 SQL 语句
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {    
        printf("INFO::准备SQL语句失败: %s\n",sqlite3_errmsg(db));
        return -1;
    }
    // 绑定参数：索引从 1 开始
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);

    // 执行插入
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        printf("INFO::执行插入失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    return 1;
}

int query_cilent_info(sqlite3* db,const char* account,char* ip,int ip_size,int* port_out){
        // 参数检查
    if (db == NULL || account == NULL || ip == NULL || port_out == NULL) {
        printf("INFO::查询语句中,参数不能为空\n");
        return -1;
    }
    // SQL查询语句
    const char* sql = "SELECT ip_address, port FROM clients WHERE account = ?;";
    sqlite3_stmt *stmt = NULL;
    // 准备sql语句
    int rc = sqlite3_prepare_v2(db,sql,-1,&stmt,NULL);
    if (rc != SQLITE_OK) {
        printf("INFO::查询语句准备失败: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    // 绑定账号参数
    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_STATIC);
    // 执行查询
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_ROW){
        // 成功获取数据
        const unsigned char* ip_text = sqlite3_column_text(stmt,0);
        int port = sqlite3_column_int(stmt,1);
        // 复制到缓冲区
        strncpy(ip,(const char*)ip_text,ip_size);
        ip[ip_size-1] = '\0';
        *port_out = port;
        printf("INFO::查询成功\n");
    }
    else if(rc == SQLITE_DONE){
        // 查询完毕后但是没找到这个账号
        printf("INFO::查询账户%s不存在\n",account);
        return 1;
    }else{ 
       printf("INFO::查询账户%s失败: %s\n",account,sqlite3_errmsg(db));
       return -1;
    }
    // 清理语句对象
    sqlite3_finalize(stmt);
    return 1;
}