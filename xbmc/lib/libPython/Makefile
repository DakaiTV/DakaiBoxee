INCLUDES=-I. -I../../ -I../../linux -I../../../guilib -I../../utils -I.
SRCS=XBPython.cpp XBPythonDll.cpp XBPyThread.cpp XBPythonDllFuncs.S XBPyPersistentThread.cpp

LIB=python.a

include ../../../Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(pathsubst %.S,,$(SRCS))))
