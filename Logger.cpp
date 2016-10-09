#include "Logger.h"
#include <stdio.h>

Logger::Logger()
{
}

void Logger::log(std::string content)
{
    printf("%s\n", content.c_str(), content.size());
}
