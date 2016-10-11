#ifndef FTPSERVER_H
#define FTPSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <string>
#include <map>
#include <list>
#include "LocalFile.h"
#include "Logger.h"

class FtpServer;

struct UserConfig
{
    std::string password;
    std::string rootPath;
};

enum ClientOperation
{
    UNKNOWN,
    AUTH,
    USER,
    PASS,
    SYST,
    FEAT,
    TYPE,
    CWD,
    CDUP,
    PWD,
    PASV,
    LIST,
    NLST,
    STOR,
    APPE,
    RETR,
    MKD,
    RMD,
    DELE,
    RNFR,
    RNTO,
    ABOR,
    NOOP,
    QUIT
};

struct ClientCommand
{
    ClientOperation op;
    std::string data;
};

enum DataType
{
    TypeI,
    TypeA
};

struct FtpClient
{
    evutil_socket_t cmdSocket;
    sockaddr_in addr;
    std::string user;
    bool login;
    std::string rootPath;   // 逻辑根目录的实际路径
    std::string curRelativePath;    // 逻辑路径
    
    bufferevent* cmdBev;
    bufferevent* pasvsBev;
    evconnlistener* pasvListener;
    ClientCommand lastCmd;  // 上一个命令
    DataType type;

    bool hasPendingCmd;
    ClientCommand pendingCmd;   // 上一个待处理的命令，一般是需要数据通道配合使用的命令，如LIST、RETR等
    std::string pendingAccessFile;  // 待处理命令关联的文件
    LocalFile* storFile;    // 收到STOR文件上传命令后，在服务器端存储的文件
    
    event* cmdTimer;    // 命令通道超时
    short cmdTickCount;
    
    FtpServer* serverPtr;   // 方便在类静态函数中操作FtpServer类
};

typedef void (FtpServer::*ProcessFunc)(FtpClient*, ClientCommand);

class FtpServer
{
public:
    FtpServer();
    ~FtpServer();

    int start();
    
protected:
    void initUserConfigs();
    void clearUserConfigs();
    
    void initCmdMaps();
    ClientOperation matchCmdOp(std::string cmd);
    ProcessFunc matchProcessFunc(ClientOperation op);
    UserConfig* findUserConfig(const std::string& user);
    
    static void listenCallback(evconnlistener* listener, evutil_socket_t fd,
                               sockaddr* address, int socklen, void* arg);
    static void readCallback(bufferevent* bev, void* arg);
    static void eventCallback(bufferevent* bev, short event, void* arg);
    
    static void pasvListenCallback(evconnlistener* listener, evutil_socket_t fd,
                                   sockaddr* address, int socklen, void* arg);
    static void pasvReadCallback(bufferevent* bev, void* arg);
    static void pasvEventCallback(bufferevent* bev, short event, void* arg);
    
    static void cmdTimerCallback(evutil_socket_t fd, short event, void* arg);

    void processUnknown(FtpClient* client, ClientCommand cmd);
    void processAuth(FtpClient* client, ClientCommand cmd);
    void processUser(FtpClient* client, ClientCommand cmd);
    void processPass(FtpClient* client, ClientCommand cmd);
    void processSyst(FtpClient* client, ClientCommand cmd);
    void processFeat(FtpClient* client, ClientCommand cmd);
    
    void processCwd(FtpClient* client, ClientCommand cmd);
    void processCdup(FtpClient* client, ClientCommand cmd);
    void processPwd(FtpClient* client, ClientCommand cmd);
    void processType(FtpClient* client, ClientCommand cmd);
    void processPasv(FtpClient* client, ClientCommand cmd);
    void processList(FtpClient* client, ClientCommand cmd);
    void echoList(FtpClient* client, const std::string& dir);
    void processNlst(FtpClient* client, ClientCommand cmd);
    
    void processRetr(FtpClient* client, ClientCommand cmd);
    void echoFile(FtpClient* client, const std::string& filename);
    void processStor(FtpClient* client, ClientCommand cmd);
    void processAppe(FtpClient* client, ClientCommand cmd);
    
    void processMkd(FtpClient* client, ClientCommand cmd);
    void processRmd(FtpClient* client, ClientCommand cmd);
    void processDele(FtpClient* client, ClientCommand cmd);
    void processRnfr(FtpClient* client, ClientCommand cmd);
    void processRnto(FtpClient* client, ClientCommand cmd);
    
    void processAbor(FtpClient* client, ClientCommand cmd);
    void processNoop(FtpClient* client, ClientCommand cmd);
    void processQuit(FtpClient* client, ClientCommand cmd);

    void echo(bufferevent* bev, std::string response, bool immediately = false);
    std::string generateAbsoluteTarget(FtpClient* client, std::string fileOrDir);
    
    evconnlistener* createListenerForPasv(evconnlistener_cb callback, FtpClient* client);
    std::string formatPasvAddress(evconnlistener* listener);
    
    FtpClient* addClient(evutil_socket_t socket);
    void removeClient(FtpClient* client);
    
    static bool isCompleteCommand(std::string& cmdStr);
    ClientCommand parseClientCommand(std::string cmdStr);
    void log(std::string& msg);
    
protected:
    event_base* m_eventBase;
    uint16_t m_cmdPort;
    short m_cmdTimeout;
    evconnlistener* m_cmdListener;
    std::map<std::string, UserConfig*> m_userConfigMap;
    std::map<evutil_socket_t, FtpClient*> m_clients;
    std::map<ClientOperation, ProcessFunc> m_processFuncMap;
    std::map<std::string, ClientOperation> m_strOpMap;
    Logger* m_logger;
};

#endif // FTPSERVER_H
