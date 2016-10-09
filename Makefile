CXX = g++
LINK = g++
CXXFLAGS = -pipe -g -Wall
INCPATH = -I/usr/local/libevent-2.0.22/include
LIBS = -L/usr/local/libevent-2.0.22/lib -levent
SOURCES = main.cpp FtpServer.cpp LocalFile.cpp Logger.cpp 
OBJECTS = main.o FtpServer.o LocalFile.o Logger.o
TARGET = ftp_server

$(TARGET): $(OBJECTS)  
	$(LINK) -o $(TARGET) $(OBJECTS) $(LIBS)

main.o: main.cpp FtpServer.h LocalFile.h Logger.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o main.o main.cpp

FtpServer.o: FtpServer.cpp LocalFile.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o FtpServer.o FtpServer.cpp

LocalFile.o: LocalFile.cpp LocalFile.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o LocalFile.o LocalFile.cpp

Logger.o: Logger.cpp Logger.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o Logger.o Logger.cpp

clean:
	rm -f *.o $(TARGET)
