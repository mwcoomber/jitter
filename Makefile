SRCDIR=src
OBJDIR=obj
BINDIR=bin
PROJNAME=jitter
CXXFLAGS=\
	-pipe\
	-std=c++26\
	-O3\
	-march=native\
	-fno-pie\
	-fno-stack-protector\
	-fno-omit-frame-pointer\
	-fno-rtti\
	-fno-exceptions\
	-fverbose-asm\
	-flto=auto\
	-ffat-lto-objects\
	-fdevirtualize-at-ltrans\
	-fipa-pta\
	-fno-semantic-interposition\
	-fno-stack-protector\
	-fno-plt\
	-floop-nest-optimize\
	-fgraphite-identity\
	-floop-parallelize-all\
	-U_FORTIFY_SOURCE\
	-U_GLIBCXX_ASSERTIONS\
	-masm=intel\
	-Werror\
	-Wall\
	-Wextra\
	-pedantic\
	-Wfatal-errors\
	#-ggdb3\
	#
LDFLAGS=\
	-flto=auto\
	-fuse-ld=mold\
	-Wl,--as-needed\
	#

TARGET=$(BINDIR)/$(PROJNAME)
SRC=$(shell find $(SRCDIR) -name '*.cpp')
OBJ=$(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRC))

default: $(TARGET)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c -o $@ $< $(CXXFLAGS)

$(OBJDIR)/%.s: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -S -o $@ $< $(CXXFLAGS)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: clean
.DEFAULT: default
