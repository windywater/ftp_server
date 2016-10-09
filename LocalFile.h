#ifndef LOCALFILE_H
#define LOCALFILE_H

#include <string>
#include <stdio.h>

class LocalFile
{
public:
    LocalFile();
    ~LocalFile();
    
    enum OpenMode
    {
        Read     = 0x1,
        Write    = 0x2,
        Truncate = 0x4
    };
    
    bool open(std::string filename, int mode);
    bool isOpen();
    void close();
    
    std::string readAll();
    std::string read(unsigned int length);
    void write(std::string bytes);
    void write(char* data, unsigned int length);
    
    static std::string getUpDir(const std::string& dir);
    static std::string getDirList(const std::string& dir);
    static bool exist(const std::string& fileOrDir);
    static bool mkDir(const std::string& dir);
    static bool rmDir(const std::string& dir);
    static bool rmFile(const std::string& file);
    static bool rename(const std::string& oldPath, const std::string& newPath);
    
protected:
    std::string m_filename;
    FILE* m_file;
    OpenMode m_openMode;
    bool m_isOpen;
    
};

#endif // LOCALFILE_H
