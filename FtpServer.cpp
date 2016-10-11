#include "FtpServer.h"
#include <memory.h>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <ifaddrs.h>
#include "LocalFile.h"

// 若未登录，大部分命令请求要回显登录提示
#define ENSURE_USER_LOGIN(client) \
    do \
    { \
        if (!client->login) \
        { \
            echo(client->cmdBev, "530 Please login with USER and PASS."); \
            return; \
        } \
    } while (0);

#define ENSURE_PARAMETERS(cmd) \
    do \
    {   \
        if (cmd.data.empty()) \
        { \
            echo(client->cmdBev, "501 Invalid parameters."); \
            return; \
        } \
    } while (0);


FtpServer::FtpServer()
{
    m_cmdPort = 5021;
    m_cmdTimeout = 60;
	m_logger = new Logger;

    initUserConfigs();
    initCmdMaps();
}

FtpServer::~FtpServer()
{
    delete m_logger;
    clearUserConfigs();
}

void FtpServer::initCmdMaps()
{
#define INSERT_CMD_MAPS(str, op, func) \
    m_processFuncMap.insert(std::make_pair(op, func)); \
    m_strOpMap.insert(std::make_pair(str, op));
    
    INSERT_CMD_MAPS("",     UNKNOWN,    &FtpServer::processUnknown)
    INSERT_CMD_MAPS("AUTH", AUTH,   &FtpServer::processAuth)
    INSERT_CMD_MAPS("USER", USER,   &FtpServer::processUser)
    INSERT_CMD_MAPS("PASS", PASS,   &FtpServer::processPass)
    INSERT_CMD_MAPS("SYST", SYST,   &FtpServer::processSyst)
    INSERT_CMD_MAPS("FEAT", FEAT,   &FtpServer::processFeat)
    INSERT_CMD_MAPS("TYPE", TYPE,   &FtpServer::processType)
    INSERT_CMD_MAPS("CWD",  CWD,    &FtpServer::processCwd)
    INSERT_CMD_MAPS("CDUP", CDUP,   &FtpServer::processCdup)
    INSERT_CMD_MAPS("PWD",  PWD,    &FtpServer::processPwd)
    INSERT_CMD_MAPS("PASV", PASV,   &FtpServer::processPasv)
    INSERT_CMD_MAPS("LIST", LIST,   &FtpServer::processList)
    INSERT_CMD_MAPS("NLST", NLST,   &FtpServer::processNlst)
    INSERT_CMD_MAPS("RETR", RETR,   &FtpServer::processRetr)
    INSERT_CMD_MAPS("STOR", STOR,   &FtpServer::processStor)
    INSERT_CMD_MAPS("APPE", APPE,   &FtpServer::processAppe)
    INSERT_CMD_MAPS("MKD",  MKD,    &FtpServer::processMkd)
    INSERT_CMD_MAPS("RMD",  RMD,    &FtpServer::processRmd)
    INSERT_CMD_MAPS("DELE", DELE,   &FtpServer::processDele)
    INSERT_CMD_MAPS("RNFR", RNFR,   &FtpServer::processRnfr)
    INSERT_CMD_MAPS("RNTO", RNTO,   &FtpServer::processRnto)
    INSERT_CMD_MAPS("ABOR", ABOR,   &FtpServer::processAbor)
    INSERT_CMD_MAPS("NOOP", NOOP,   &FtpServer::processNoop)
    INSERT_CMD_MAPS("QUIT", QUIT,   &FtpServer::processQuit)
}

ClientOperation FtpServer::matchCmdOp(std::string cmd)
{
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    std::map<std::string, ClientOperation>::iterator it = m_strOpMap.find(cmd);
    if (it == m_strOpMap.end())
        return UNKNOWN;
    
    return it->second;
}

ProcessFunc FtpServer::matchProcessFunc(ClientOperation op)
{
    std::map<ClientOperation, ProcessFunc>::iterator it = m_processFuncMap.find(op);
    if (it == m_processFuncMap.end())
        return NULL;
    
    return it->second;
}

UserConfig* FtpServer::findUserConfig(const std::string& user)
{
    std::map<std::string, UserConfig*>::iterator it = m_userConfigMap.find(user);
    if (it == m_userConfigMap.end())
        return NULL;
    
    return it->second;
}

void FtpServer::initUserConfigs()
{
    UserConfig* cfg = new UserConfig;
    cfg->password = "test";
    cfg->rootPath = "/home";
    m_userConfigMap.insert(std::make_pair("test", cfg));
}

void FtpServer::clearUserConfigs()
{
    std::map<std::string, UserConfig*>::iterator it = m_userConfigMap.begin();
    for (; it != m_userConfigMap.end(); it++)
        delete it->second;
    
    m_userConfigMap.clear();
}

int FtpServer::start()
{
    m_eventBase = event_base_new();
    if (m_eventBase == NULL)
        return -1;

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(m_cmdPort);
    
    m_cmdListener = evconnlistener_new_bind(m_eventBase, FtpServer::listenCallback, 
        this, LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&addr, sizeof(addr));
    if (m_cmdListener == NULL)
        return -1;

    event_base_dispatch(m_eventBase);
    evconnlistener_free(m_cmdListener);
	event_base_free(m_eventBase);
    
    return 0;
}

/*static*/ void FtpServer::listenCallback(evconnlistener* listener, evutil_socket_t fd,
        sockaddr* address, int socklen, void* arg)

{
    FtpServer* thisPtr = (FtpServer*)arg;
    event_base* base = (event_base*)thisPtr->m_eventBase;
    bufferevent* bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    
    FtpClient* client = thisPtr->addClient(fd);
    client->serverPtr = thisPtr;
    client->cmdBev = bev;
    client->addr = *((sockaddr_in*)address);
    
    // 命令通道超时检测
    client->cmdTimer = event_new(base, -1, EV_PERSIST, FtpServer::cmdTimerCallback, client);
    struct timeval tv;
    evutil_timerclear(&tv);
	tv.tv_sec = 1;
	event_add(client->cmdTimer, &tv);
    
    bufferevent_setcb(bev, FtpServer::readCallback, NULL, FtpServer::eventCallback, client);
    bufferevent_enable(bev, EV_READ|EV_WRITE|EV_PERSIST);

    thisPtr->echo(client->cmdBev, "220 Hello.");
}

/*static*/ void FtpServer::readCallback(bufferevent* bev, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    FtpServer* serverPtr = client->serverPtr;
    
    static const int BUF_SIZE = 1024;
    static char buf[BUF_SIZE];
    int count;

    count = bufferevent_read(bev, buf, BUF_SIZE);
    if (count > 0)
    {
        // 计时归零
        client->cmdTickCount = 0;
        std::string request(buf, count);

        if (isCompleteCommand(request))
        {
            ClientCommand cmd = serverPtr->parseClientCommand(request);
            ProcessFunc func = serverPtr->matchProcessFunc(cmd.op);
            (serverPtr->*func)(client, cmd);
        }
    }
}

/*static*/ bool FtpServer::isCompleteCommand(std::string& cmdStr)
{
    if (cmdStr.size() < 2)
        return false;
    
    if (cmdStr.at(cmdStr.size()-2) == '\r' &&
        cmdStr.at(cmdStr.size()-1) == '\n')
    {
        return true;
    }
    
    return false;
}

ClientCommand FtpServer::parseClientCommand(std::string cmdStr)
{
    ClientCommand cmd;
    
    size_t space = cmdStr.find(' ');
    if (space == std::string::npos)
    {
        cmd.op = matchCmdOp(cmdStr.substr(0, cmdStr.size()-2));
    }
    else
    {
        cmd.op = matchCmdOp(cmdStr.substr(0, space));
        size_t noSpace = cmdStr .find_first_not_of(' ', space+1);
        cmd.data = cmdStr.substr(noSpace, cmdStr.size()-noSpace-2);
    }

    return cmd;
}

/*static*/ void FtpServer::eventCallback(bufferevent* bev, short event, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    FtpServer* serverPtr = client->serverPtr;
    
    // 命令通道客户端关闭连接
    if (event & BEV_EVENT_EOF)
    {
        serverPtr->removeClient(client);
    }
}

FtpClient* FtpServer::addClient(evutil_socket_t socket)
{
    FtpClient* client = new FtpClient;
    client->cmdSocket = socket;
    client->pasvListener = NULL;
    client->pasvsBev = NULL;
    client->hasPendingCmd = false;
    client->login = false;
    client->type = TypeI;
    
    m_clients.insert(std::make_pair(socket, client));
    return client;
}

void FtpServer::removeClient(FtpClient* client)
{
    std::map<evutil_socket_t, FtpClient*>::iterator it = m_clients.find(client->cmdSocket);
    if (it == m_clients.end())
        return;
    
    if (client->cmdBev != NULL)
    {
        bufferevent_free(client->cmdBev);
        client->cmdBev = NULL;
    }
    
    if (client->pasvListener != NULL)
    {
        evconnlistener_free(client->pasvListener);
        client->pasvListener = NULL;
    }
    
    if (client->pasvsBev != NULL)
    {
        bufferevent_free(client->pasvsBev);
        client->pasvsBev = NULL;
    }
    
    if (client->cmdTimer != NULL)
    {
        evtimer_del(client->cmdTimer);
        event_free(client->cmdTimer);
        client->cmdTimer = NULL;
    }
    
    delete it->second;
    m_clients.erase(it);
}

void FtpServer::processUnknown(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "500 Command not understood.");
}

void FtpServer::processAuth(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "534 Not support.");
}

void FtpServer::processUser(FtpClient* client, ClientCommand cmd)
{
    client->login = false;
    client->user = cmd.data;
    echo(client->cmdBev, "331 Password required for " + client->user + ".");
}

void FtpServer::processPass(FtpClient* client, ClientCommand cmd)
{
    if (client->user.empty())
    {
        echo(client->cmdBev, "503 Login with USER first.");
        return;
    }
    
    if (cmd.data.empty())
    {
        echo(client->cmdBev, "530 User cannot log in.");
        return;
    }
    
    UserConfig* cfg = findUserConfig(client->user);
    if (cfg == NULL)
    {
        echo(client->cmdBev, "530 User cannot log in.");
        return;
    }
    
    if (cmd.data == cfg->password)
    {
        echo(client->cmdBev, "230 User logged in.");
        client->login = true;
        client->rootPath = cfg->rootPath;
        client->curRelativePath = "/";
    }
    else
    {
        echo(client->cmdBev, "530 User cannot log in.");
    }
}

void FtpServer::processSyst(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "215 Windows_NT");
}

void FtpServer::processFeat(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "211-Extended features supported:\r\n UTF8\r\n211 END");
}

void FtpServer::processCwd(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)

    std::string newRelPath;
    if (cmd.data.size() > 0 && cmd.data[0] == '/')  // 切换到绝对路径
        newRelPath = cmd.data;
    else    // 相对路径
    {
        if (client->curRelativePath == "/")
            newRelPath = client->curRelativePath + cmd.data;
        else
            newRelPath = client->curRelativePath + "/" + cmd.data;
    }
    
    std::string newDir = client->rootPath + newRelPath;
    if (LocalFile::exist(newDir))
    {
        client->curRelativePath = newRelPath;
        echo(client->cmdBev, "250 CWD command successful.");
    }
    else
    {
        echo(client->cmdBev, "550 The system cannot find the file specified.");
    }
}

void FtpServer::processCdup(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    
    client->curRelativePath = LocalFile::getUpDir(client->curRelativePath);
    echo(client->cmdBev, "250 CDUP command successful.");
}

void FtpServer::processPwd(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)

    echo(client->cmdBev, "257 \"" + client->curRelativePath + "\" is current directory.");
}

void FtpServer::processType(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    if (cmd.data == "i" || cmd.data == "I")
    {
        echo(client->cmdBev, "200 Type set to I.");
        client->type = TypeI;
    }
    else if (cmd.data == "a" || cmd.data == "A")
    {
        echo(client->cmdBev, "200 Type set to A.");
        client->type = TypeA;
    }
    else
    {
        echo(client->cmdBev, "501 Parameter error.");
    }
}

void FtpServer::processPasv(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    
    evconnlistener* pasvListener = createListenerForPasv(FtpServer::pasvListenCallback, client);
    client->pasvListener = pasvListener;

    // 回显服务端IP地址和可用端口
    std::string address = formatPasvAddress(pasvListener);
    std::string response = "227 Entering Passive Mode ("+address+").";
    echo(client->cmdBev, response);
}

std::string FtpServer::formatPasvAddress(evconnlistener* listener)
{
    evutil_socket_t socket = evconnlistener_get_fd(listener);
    
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, len);
    getsockname(socket, (sockaddr*)&addr, &len);
    uint16_t hostPort = ntohs(addr.sin_port);
    
    in_addr_t ip = 0;
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) != -1)
    {
        struct ifaddrs* ifa = ifaddr;
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL)
                continue;
            
            int family = ifa->ifa_addr->sa_family;
            if ((family == AF_INET || family == AF_INET6) &&
                std::string(ifa->ifa_name) == "eth0")
            {
                sockaddr_in addr = *((sockaddr_in*)ifa->ifa_addr);
                ip = ntohl(addr.sin_addr.s_addr);
                break;
            }
        }
        
        freeifaddrs(ifaddr);
    }

    char buf[32];
    sprintf(buf, "%d,%d,%d,%d,%d,%d", 
            (ip & 0xff000000) >> 24,
            (ip & 0xff0000) >> 16,
            (ip & 0xff00) >> 8,
            ip & 0xff,
            hostPort / 256,
            hostPort % 256);
    
    return buf;
}

evconnlistener* FtpServer::createListenerForPasv(evconnlistener_cb callback, FtpClient* client)
{
    evconnlistener* pasvListener = NULL;
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
    
    // 寻找可用端口
    for (uint16_t port = 40000; port < 45000; port++)
    {
        addr.sin_port = htons(port);
        pasvListener = evconnlistener_new_bind(m_eventBase, callback, 
            client, LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
            (struct sockaddr*)&addr, sizeof(addr));
        if (pasvListener != NULL)
            return pasvListener;
    }
    
    return NULL;
}

void FtpServer::processList(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    
    echo(client->cmdBev, "150 Opening BINARY mode data connection.");
    
    std::string target = generateAbsoluteTarget(client, cmd.data);
    if (client->pasvsBev != NULL)
    {
        // 客户端先连上了PASV数据通道，直接回写目录
        echoList(client, target);
    }
    else
    {
        // 客户端还没有连上PASV数据通道，先记录，一旦连上即回显
        client->hasPendingCmd = true;
        client->pendingCmd = cmd;
        client->pendingAccessFile = target;
    }
}

void FtpServer::echoList(FtpClient* client, const std::string& dir)
{
    std::string dirRes = LocalFile::getDirList(dir);
    echo(client->pasvsBev, dirRes, true);   // 立即发送，不通过libevent的缓存队列
    echo(client->cmdBev, "226 Transfer complete.");
    
    bufferevent_free(client->pasvsBev);
    client->pasvsBev = NULL;
    evconnlistener_free(client->pasvListener);
    client->pasvListener = NULL;
}

void FtpServer::processNlst(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
}

void FtpServer::processRetr(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    echo(client->cmdBev, "150 Opening BINARY mode data connection.");
    
    std::string target = generateAbsoluteTarget(client, cmd.data);
    if (client->pasvsBev != NULL)
    {
        echoFile(client, target);
    }
    else
    {
        client->hasPendingCmd = true;
        client->pendingCmd = cmd;
        client->pendingAccessFile = target;
    }
}

void FtpServer::echoFile(FtpClient* client, const std::string& filename)
{
    LocalFile file;
    file.open(filename, LocalFile::Read);
    std::string content = file.readAll();
    file.close();
    
    echo(client->pasvsBev, content, true);
    echo(client->cmdBev, "226 Transfer complete.");
    
    bufferevent_free(client->pasvsBev);
    client->pasvsBev = NULL;
    evconnlistener_free(client->pasvListener);
    client->pasvListener = NULL;
    client->hasPendingCmd = false;
}

void FtpServer::processStor(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    client->storFile = new LocalFile;
    std::string target = generateAbsoluteTarget(client, cmd.data);
    bool ret = client->storFile->open(target, LocalFile::Write|LocalFile::Truncate);
    
    if (ret)
    {
        client->hasPendingCmd = true;
        client->pendingCmd = cmd;
        client->pendingAccessFile = target;
        
        echo(client->cmdBev, "125 Data connection already open; Transfer starting.");
    }
    else
    {
        echo(client->cmdBev, "550 File open failed.");
        delete client->storFile;
        client->storFile = NULL;
    }
}

void FtpServer::processAppe(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
            
    client->storFile = new LocalFile;
    std::string target = generateAbsoluteTarget(client, cmd.data);
    bool ret = client->storFile->open(target, LocalFile::Write);
    
    if (ret)
    {
        client->hasPendingCmd = true;
        client->pendingCmd = cmd;
        client->pendingAccessFile = target;
        
        echo(client->cmdBev, "125 Data connection already open; Transfer starting.");
    }
    else
    {
        echo(client->cmdBev, "550 File open failed.");
        delete client->storFile;
        client->storFile = NULL;
    }
}

void FtpServer::processMkd(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    std::string absoluteDir = generateAbsoluteTarget(client, cmd.data);
    if (LocalFile::exist(absoluteDir))
    {
        echo(client->cmdBev, "550 Cannot create a directory when the directory already exists.");
        return;
    }
    
    bool ret = LocalFile::mkDir(absoluteDir);
    if (ret)
        echo(client->cmdBev, "257 \"" + cmd.data + "\" directory created.");
    else
        echo(client->cmdBev, "550 Directory create failed.");
}

void FtpServer::processRmd(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    std::string absoluteDir = generateAbsoluteTarget(client, cmd.data);
    if (!LocalFile::exist(absoluteDir))
    {
        echo(client->cmdBev, "550 The direcotory cannot be found.");
        return;
    }
    
    bool ret = LocalFile::rmDir(absoluteDir);
    if (ret)
        echo(client->cmdBev, "RMD command successful.");
    else
        echo(client->cmdBev, "RMD command failed.");
}

void FtpServer::processDele(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
    
    std::string absoluteFile = generateAbsoluteTarget(client, cmd.data);
    if (!LocalFile::exist(absoluteFile))
    {
        echo(client->cmdBev, "550 The file cannot be found.");
        return;
    }
    
    bool ret = LocalFile::rmFile(absoluteFile);
    if (ret)
        echo(client->cmdBev, "250 DELE command successful.");
    else
        echo(client->cmdBev, "550 DELE command failed.");
}

void FtpServer::processRnfr(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)

    std::string target = generateAbsoluteTarget(client, cmd.data);
    if (!LocalFile::exist(target))
    {
        echo(client->cmdBev, "550 The system cannot find the file specified.");
        return;
    }
    
    client->hasPendingCmd = true;
    client->pendingCmd = cmd;
    client->pendingAccessFile = target;
    
    echo(client->cmdBev, "350 Requested file action pending further information.");    
}

void FtpServer::processRnto(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
    ENSURE_PARAMETERS(cmd)
            
    if (client->hasPendingCmd && client->pendingCmd.op == RNFR)
    {
        std::string newTarget = generateAbsoluteTarget(client, cmd.data);
        bool ret = LocalFile::rename(client->pendingAccessFile, newTarget);
        if (ret)
            echo(client->cmdBev, "250 RNTO command successful.");
        else
            echo(client->cmdBev, "550 RNTO command failed.");
        
        client->hasPendingCmd = false;
    }
    else
    {
        echo(client->cmdBev, "503 Bad sequence of commands.");
    }
}

void FtpServer::processAbor(FtpClient* client, ClientCommand cmd)
{
    ENSURE_USER_LOGIN(client)
}

void FtpServer::processNoop(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "200 NOOP command successful.");
}

void FtpServer::processQuit(FtpClient* client, ClientCommand cmd)
{
    echo(client->cmdBev, "221 Bye.", true);
    removeClient(client);
}

/*static*/ void FtpServer::pasvListenCallback(evconnlistener* listener, evutil_socket_t fd,
    sockaddr* address, int socklen, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    
    bufferevent* pasvBev = bufferevent_socket_new(bufferevent_get_base(client->cmdBev), fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(pasvBev, FtpServer::pasvReadCallback, NULL, FtpServer::pasvEventCallback, arg);
    bufferevent_enable(pasvBev, EV_READ|EV_WRITE|EV_PERSIST);
    client->pasvsBev = pasvBev;
    
    // 处理之前待处理的命令
    if (client->hasPendingCmd)
    {
        if (client->pendingCmd.op == LIST)
        {
            client->serverPtr->echoList(client, client->pendingAccessFile);
            client->hasPendingCmd = false;
        }
        else if (client->pendingCmd.op == RETR)
        {
            client->serverPtr->echoFile(client, client->pendingAccessFile);
            client->hasPendingCmd = false;
        }
    }
}

/*static*/ void FtpServer::pasvReadCallback(bufferevent* bev, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    FtpServer* serverPtr = client->serverPtr;
    
    static const int BUF_SIZE = 8*1024;
    static char buf[BUF_SIZE];
    size_t length;

    length = bufferevent_read(bev, buf, BUF_SIZE);
    
    // 处理客户端上传的文件
    if (client->hasPendingCmd &&
        (client->pendingCmd.op == STOR || client->pendingCmd.op == APPE))
    {
        client->storFile->write(buf, length);
    }
}

/*static*/ void FtpServer::pasvEventCallback(bufferevent* bev, short event, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    FtpServer* serverPtr = client->serverPtr;
    
    if (event & BEV_EVENT_EOF)
    {
        // 客户端在数据通道主动关闭，表示文件上传完成
        if (client->hasPendingCmd && 
            (client->pendingCmd.op == STOR || client->pendingCmd.op == APPE))
        {
            serverPtr->echo(client->cmdBev, "226 Transfer complete.");
            
            client->storFile->close();
            delete client->storFile;
            client->storFile = NULL;
            
            bufferevent_free(client->pasvsBev);
            client->pasvsBev = NULL;
            evconnlistener_free(client->pasvListener);
            client->pasvListener = NULL;
            client->hasPendingCmd = false;
        }
    }
}

/*static*/ void FtpServer::cmdTimerCallback(evutil_socket_t fd, short event, void* arg)
{
    FtpClient* client = (FtpClient*)arg;
    FtpServer* serverPtr = client->serverPtr;
    
    // 超时则主动关闭命令客户端
    client->cmdTickCount++;
    if (client->cmdTickCount >= serverPtr->m_cmdTimeout)
        serverPtr->removeClient(client);
}

void FtpServer::echo(bufferevent* bev, std::string response, bool immediately /*= false*/)
{
    response += "\r\n";
    
    if (immediately)
        send(bufferevent_getfd(bev), response.c_str(), response.size(), 0);
    else
        bufferevent_write(bev, response.c_str(), response.size());
}

std::string FtpServer::generateAbsoluteTarget(FtpClient* client, std::string fileOrDir)
{
    if (fileOrDir.empty())
        return client->rootPath + client->curRelativePath;
    
    if (fileOrDir.at(0) == '/')
        return client->rootPath + fileOrDir;
    
    return client->rootPath + client->curRelativePath + "/" + fileOrDir;
}

void FtpServer::log(std::string& msg)
{
    m_logger->log(msg);
}
