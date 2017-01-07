
LDLIBS=-lpthread
CPPFLAGS+=-std=c++14 -g

SRCS:=source/main.cpp
OBJS=$(subst .cpp,.o,$(SRCS))


COMPILE.cpp = $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH)



all : moltengamepadctl



moltengamepadctl : $(SRCS)
	$(COMPILE.cpp) $(LDFLAGS) -o moltengamepadctl $(SRCS) $(LDLIBS)

clean :
	$(RM) moltengamepadctl

.PHONY: debug
debug : CPPFLAGS+=-DDEBUG -g
debug : moltengamepadctl
