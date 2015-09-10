CXXFLAGS+=-I ../include 
../bin/test: ../bin/$(LIBNAME).a ../bin/$(LIBNAME).so test.o test-barrier.o
	$(CXX) $(CXXFLAGS) -o ../bin/test test.o -L ../bin/ -lpthread -lpbsm 
	$(CXX) $(CXXFLAGS) -o ../bin/test-barrier test-barrier.o -L ../bin/ -lpthread -lpbsm 

test.o: test.cpp

test-barrier.o: test-barrier.cpp

.PHONY: clean

clean:
	-rm -fr *.o ../bin/test ../bin/test-barrier
