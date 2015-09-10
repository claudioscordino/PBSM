INCLUDE_DIR = ../include
OBJECTS = policy.o logger.o communication_handler.o
INCLUDES = $(INCLUDE_DIR)/*.hpp
CXXFLAGS += -I$(INCLUDE_DIR) 

all: ../bin/$(LIBNAME).a ../bin/$(LIBNAME).so 

../bin/$(LIBNAME).a: $(OBJECTS)
	ar rcs ../bin/$(LIBNAME).a $(OBJECTS)

../bin/$(LIBNAME).so: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -shared -Wl,-soname,$(LIBNAME).so -o ../bin/$(LIBNAME).so.$(LIBVERSION) $(OBJECTS) -lpthread #-lrt
	ln -f -s $(LIBNAME).so.$(LIBVERSION) ../bin/$(LIBNAME).so

policy.o: policy.cpp $(INCLUDES)

logger.o: logger.cpp $(INCLUDES)

communication_handler.o: communication_handler.cpp $(INCLUDES)

.PHONY: clean

clean:
	-rm -fr *.o ../bin/$(LIBNAME)* 

