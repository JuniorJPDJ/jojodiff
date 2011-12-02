################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/JDebug.cpp \
../src/JDiff.cpp \
../src/JFileAhead.cpp \
../src/JFileIStream.cpp \
../src/JFileIStreamAhead.cpp \
../src/JHashPos.cpp \
../src/JMatchTable.cpp \
../src/JOutAsc.cpp \
../src/JOutBin.cpp \
../src/JOutRgn.cpp \
../src/main.cpp 

OBJS += \
./src/JDebug.o \
./src/JDiff.o \
./src/JFileAhead.o \
./src/JFileIStream.o \
./src/JFileIStreamAhead.o \
./src/JHashPos.o \
./src/JMatchTable.o \
./src/JOutAsc.o \
./src/JOutBin.o \
./src/JOutRgn.o \
./src/main.o 

CPP_DEPS += \
./src/JDebug.d \
./src/JDiff.d \
./src/JFileAhead.d \
./src/JFileIStream.d \
./src/JFileIStreamAhead.d \
./src/JHashPos.d \
./src/JMatchTable.d \
./src/JOutAsc.d \
./src/JOutBin.d \
./src/JOutRgn.d \
./src/main.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


