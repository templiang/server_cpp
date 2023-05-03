CXX ?= g++


all:servers

server: main.cc common/connecter/SqlConnectionPool.cpp common/http/Http.cpp common/timer/Timer.cpp common/WebServer.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

.PHONY:clean
clean:
	rm  -r server