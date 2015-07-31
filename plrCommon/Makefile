CC        = gcc
CXX       = g++
COMFLAGS  = -fPIC -Wall  -Wextra -Werror -O3 -MMD -pthread
CFLAGS    = -std=gnu99
CXXFLAGS  = -std=c++11
LDFLAGS   = -shared -Wl,--no-undefined -pthread
LDLIBS    = -lrt

OBJDIR    = obj
CFILES    = $(wildcard *.c)
CXXFILES  = $(wildcard *.cpp)
OBJ       = $(CFILES:%.c=$(OBJDIR)/%.o) $(CXXFILES:%.cpp=$(OBJDIR)/%.o)
DEP       = $(OBJ:%.o=%.d)

all: libplrCommon.so

libplrCommon.so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
  
# Include .d dependency files created by -MMD flag
-include $(DEP)

# Rule to build any C source files
$(OBJDIR)/%.o: %.c
	$(CC) $(COMFLAGS) $(CFLAGS) -c $< -o $@
  
# Rule to build any C++ source files
$(OBJDIR)/%.o: %.cpp
	$(CXX) $(COMFLAGS) $(CXXFLAGS) -c $< -o $@

# Rules so that OBJDIR is created if it doesn't exist
$(OBJ): | $(OBJDIR)
$(OBJDIR):
	mkdir -p $@
  
clean:
	$(RM) -R $(OBJDIR) libplrCommon.so

.PHONY : all clean
