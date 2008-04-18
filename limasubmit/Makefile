include makeinclude

OBJ	= main.o ../limadb.o ../limatools.o

all: limasubmit.exe
	mkapp limasubmit

limasubmit.exe: $(OBJ)
	$(LD) $(LDFLAGS) -o limasubmit.exe $(OBJ) $(LIBS)

clean:
	rm -f *.o *.exe
	rm -rf limasubmit.app
	rm -f limasubmit

makeinclude:
	@echo please run ./configure
	@false

SUFFIXES: .cpp .o
.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $<
