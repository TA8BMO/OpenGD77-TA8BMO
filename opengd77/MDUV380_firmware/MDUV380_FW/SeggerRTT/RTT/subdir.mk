################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SeggerRTT/RTT/SEGGER_RTT.c \
../SeggerRTT/RTT/SEGGER_RTT_printf.c 

S_UPPER_SRCS += \
../SeggerRTT/RTT/SEGGER_RTT_ASM_ARMv7M.S 

OBJS += \
./SeggerRTT/RTT/SEGGER_RTT.o \
./SeggerRTT/RTT/SEGGER_RTT_ASM_ARMv7M.o \
./SeggerRTT/RTT/SEGGER_RTT_printf.o 

S_UPPER_DEPS += \
./SeggerRTT/RTT/SEGGER_RTT_ASM_ARMv7M.d 

C_DEPS += \
./SeggerRTT/RTT/SEGGER_RTT.d \
./SeggerRTT/RTT/SEGGER_RTT_printf.d 


# Each subdirectory must supply rules for building sources it contributes
SeggerRTT/RTT/%.o SeggerRTT/RTT/%.su SeggerRTT/RTT/%.cyclo: ../SeggerRTT/RTT/%.c SeggerRTT/RTT/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
SeggerRTT/RTT/%.o: ../SeggerRTT/RTT/%.S SeggerRTT/RTT/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m4 -g3 -DPLATFORM_MDUV380 -DDEBUG -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-SeggerRTT-2f-RTT

clean-SeggerRTT-2f-RTT:
	-$(RM) ./SeggerRTT/RTT/SEGGER_RTT.cyclo ./SeggerRTT/RTT/SEGGER_RTT.d ./SeggerRTT/RTT/SEGGER_RTT.o ./SeggerRTT/RTT/SEGGER_RTT.su ./SeggerRTT/RTT/SEGGER_RTT_ASM_ARMv7M.d ./SeggerRTT/RTT/SEGGER_RTT_ASM_ARMv7M.o ./SeggerRTT/RTT/SEGGER_RTT_printf.cyclo ./SeggerRTT/RTT/SEGGER_RTT_printf.d ./SeggerRTT/RTT/SEGGER_RTT_printf.o ./SeggerRTT/RTT/SEGGER_RTT_printf.su

.PHONY: clean-SeggerRTT-2f-RTT

