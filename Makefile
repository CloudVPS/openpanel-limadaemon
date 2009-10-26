include makeinclude

OBJ	= main.o limadb.o limatools.o

all: limadaemon.exe limacreate limaremove
	grace mkapp limadaemon
	
limadaemon.exe: $(OBJ)
	$(LD) $(LDFLAGS) -o limadaemon.exe $(OBJ) $(LIBS)

limacreate: createlist.o limadb.o
	$(LD) $(LDFLAGS) -o limacreate createlist.o limadb.o $(LIBS)

limaremove: removelist.o limadb.o
	$(LD) $(LDFLAGS) -o limaremove removelist.o limadb.o $(LIBS)

install: all
	./mkinstall.sh

clean:
	rm -f *.o *.exe
	rm -rf limadaemon.app
	rm -f limadaemon
	rm -f limacreate limaremove

makeinclude:
	@echo please run ./configure
	@false

SUFFIXES: .cpp .o
.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<
