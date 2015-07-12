CC        = gcc
CXX       = g++
COMFLAGS  = -Wall -Wextra -Werror -O3 -MMD -pthread -IplrCommon
CFLAGS    = -std=gnu99
CXXFLAGS  = -std=c++11
LDFLAGS   = -pthread -Wl,-rpath='$$ORIGIN/lib' -Llib
LDLIBS    = -lrt -lplrCommon

OBJDIR    = obj
LIBDIR    = lib

CFILES    = $(wildcard *.c)
CXXFILES  = $(wildcard *.cpp)
OBJ       = $(CFILES:%.c=$(OBJDIR)/%.o) $(CXXFILES:%.cpp=$(OBJDIR)/%.o)
DEP       = $(OBJ:%.o=%.d)

LIBNAMES  = plrCommon plrPintool plrPreload
LIBS      = $(LIBNAMES:%=$(LIBDIR)/lib%.so)

###############################################################################

all: $(LIBNAMES) pinFaultInject plr

clean:
	$(RM) -R $(OBJDIR) $(LIBDIR) plr
	$(MAKE) -C plrPintool clean extraclean
	$(MAKE) -C plrPreload clean
	$(MAKE) -C plrCommon clean
	$(MAKE) -C pinFaultInject clean extraclean

.PHONY : all clean $(LIBNAMES) pinFaultInject

###############################################################################

plr: $(OBJ) | $(LIBS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Include .d dependency files created by -MMD flag
-include $(DEP)

# Rules so that OBJDIR is created if it doesn't exist
$(OBJ): | $(OBJDIR)
$(OBJDIR):
	mkdir -p $@

# Rule to build any C source files
$(OBJDIR)/%.o: %.c
	$(CC) $(COMFLAGS) $(CFLAGS) -c $< -o $@

# Rule to build any C++ source files
$(OBJDIR)/%.o: %.cpp
	$(CXX) $(COMFLAGS) $(CXXFLAGS) -c $< -o $@

###############################################################################

$(LIBNAMES): %: | $(LIBDIR)
	$(MAKE) -C $@ lib$@.so
	ln -sf ../$@/lib$@.so $(LIBDIR)
  
pinFaultInject: | $(LIBDIR)
	$(MAKE) -C $@ $@.so
	ln -sf ../$@/$@.so $(LIBDIR)

$(LIBDIR):
	mkdir -p $@
