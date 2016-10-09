#include "LocalFile.h"
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>   
#include <time.h>
#include <sys/time.h>

LocalFile::LocalFile()
{
    m_file = NULL;
    m_openMode = Read;
    m_isOpen = false;
}

LocalFile::~LocalFile()
{
}

bool LocalFile::open(std::string filename, int mode)
{
    if (isOpen())
        close();

    m_filename = filename;
    
    std::string modeStr;
    if (mode == Read)
    {
        modeStr = "r";
    }
    else if (mode == Write)
    {
        modeStr = "a";
    }
    else if (mode == Read|Write)
    {
        modeStr = "a+";
    }
    else if (mode == Write|Truncate)
    {
        modeStr = "w";
    }
    else
    {
        modeStr = "r";
    }
    
    m_file = fopen(filename.c_str(), modeStr.c_str());
    m_isOpen = (m_file != NULL);
    return m_isOpen;
}

bool LocalFile::isOpen()
{
    return m_isOpen;
}

void LocalFile::close()
{
    if (isOpen())
    {
        fclose(m_file);
        m_isOpen = false;
    }
}

std::string LocalFile::readAll()
{
    struct stat st;
    int ret = stat(m_filename.c_str(), &st);
    if (ret == -1)
        return "";
    
    return read(st.st_size);
}

std::string LocalFile::read(unsigned int length)
{
    char* buf = new char[length];
    fread(buf, 1, length, m_file);
    std::string content(buf, length);
    delete[] buf;
    
    return content;
}

void LocalFile::write(std::string bytes)
{
    fwrite(bytes.c_str(), 1, bytes.size(), m_file);
}

void LocalFile::write(char* data, unsigned int length)
{
    fwrite(data, 1, length, m_file);
}

/*static*/ std::string LocalFile::getUpDir(const std::string& dir)
{
    if (dir == "/")
        return "/";
    
    size_t lastSplash = dir.find_last_of('/');
    if (lastSplash == std::string::npos)
        return "/";
    
    std::string upDir = dir.substr(0, lastSplash);
    if (upDir.empty())
        upDir = "/";
    
    return upDir;
}

/*static*/ std::string LocalFile::getDirList(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (d == NULL)
        return "";
    
    std::string listResult;
    chdir(dir.c_str());
    char timeBuf[32];
    char sizeBuf[16];
    for (;;)
    {
        dirent* entry = readdir(d);
        if (entry == NULL)
            break;
        struct stat st;
        lstat(entry->d_name, &st);
        std::string name(entry->d_name);
        if (name == "." || name == "..")
        {
            continue;
        }
        
        strftime(timeBuf, 32, "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
        listResult += timeBuf;
        
        if (S_ISDIR(st.st_mode))
        {
            listResult += " <DIR> ";
        }
        else
        {
            sprintf(sizeBuf, " %d ", st.st_size);
            listResult += sizeBuf;
        }
        
        listResult += entry->d_name;
        listResult += "\r\n";
    }
    
    return listResult;
    
    
#if 0
    //void printdir(char *dir, int depth)   
    {   
        DIR *dp;   
        struct dirent *entry;   
        struct stat statbuf;   
        if((dp = opendir(dir)) == NULL) {   
            fprintf(stderr,"cannot open directory: %s/n", dir);   
            return;   
        }   
        chdir(dir);   
        while((entry = readdir(dp)) != NULL) {   
            lstat(entry->d_name,&statbuf);   
            if(S_ISDIR(statbuf.st_mode)) {   
                /**//* Found a directory, but ignore . and .. */  
                if(strcmp(".",entry->d_name) == 0 ||    
                    strcmp("..",entry->d_name) == 0)   
                    continue;   
                printf("%*s%s//n",depth,"",entry->d_name);   
                /**//* Recurse at a new indent level */  
                printdir(entry->d_name,depth+4);   
            }   
            else printf("%*s%s/n",depth,"",entry->d_name);   
        }   
        chdir("..");   
        closedir(dp);   
    }   
#endif
}

/*static*/ bool LocalFile::exist(const std::string& fileOrDir)
{
    struct stat st;
    return (stat(fileOrDir.c_str(), &st) == 0);
}

/*static*/ bool LocalFile::mkDir(const std::string& dir)
{
    return (mkdir(dir.c_str(), S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH) == 0);
}

/*static*/ bool LocalFile::rmDir(const std::string& dir)
{
    return (rmdir(dir.c_str()) == 0);
}

/*static*/ bool LocalFile::rmFile(const std::string& file)
{
    return (unlink(file.c_str()) == 0);
}

/*static*/ bool LocalFile::rename(const std::string& oldPath, const std::string& newPath)
{
    return (::rename(oldPath.c_str(), newPath.c_str()) == 0);
}
