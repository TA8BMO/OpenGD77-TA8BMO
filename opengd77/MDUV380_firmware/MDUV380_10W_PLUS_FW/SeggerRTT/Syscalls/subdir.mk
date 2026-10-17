################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.c \
../SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.c \
../SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.c \
../SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.c 

OBJS += \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.o \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.o \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.o \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.o 

C_DEPS += \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.d \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.d \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.d \
./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.d 


# Each subdirectory must supply rules for building sources it contributes
SeggerRTT/Syscalls/%.o SeggerRTT/Syscalls/%.su SeggerRTT/Syscalls/%.cyclo: ../SeggerRTT/Syscalls/%.c SeggerRTT/Syscalls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-SeggerRTT-2f-Syscalls

clean-SeggerRTT-2f-Syscalls:
	-$(RM) ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.cyclo ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.d ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.o ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_GCC.su ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.cyclo ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.d ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.o ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_IAR.su ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.cyclo ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.d ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.o ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_KEIL.su ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.cyclo ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.d ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.o ./SeggerRTT/Syscalls/SEGGER_RTT_Syscalls_SES.su

.PHONY: clean-SeggerRTT-2f-Syscalls

