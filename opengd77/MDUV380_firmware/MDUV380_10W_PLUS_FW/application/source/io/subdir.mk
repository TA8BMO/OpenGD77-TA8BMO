################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/io/Leds.c \
../application/source/io/buttons.c \
../application/source/io/display.c \
../application/source/io/keyboard.c 

OBJS += \
./application/source/io/Leds.o \
./application/source/io/buttons.o \
./application/source/io/display.o \
./application/source/io/keyboard.o 

C_DEPS += \
./application/source/io/Leds.d \
./application/source/io/buttons.d \
./application/source/io/display.d \
./application/source/io/keyboard.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/io/%.o application/source/io/%.su application/source/io/%.cyclo: ../application/source/io/%.c application/source/io/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-io

clean-application-2f-source-2f-io:
	-$(RM) ./application/source/io/Leds.cyclo ./application/source/io/Leds.d ./application/source/io/Leds.o ./application/source/io/Leds.su ./application/source/io/buttons.cyclo ./application/source/io/buttons.d ./application/source/io/buttons.o ./application/source/io/buttons.su ./application/source/io/display.cyclo ./application/source/io/display.d ./application/source/io/display.o ./application/source/io/display.su ./application/source/io/keyboard.cyclo ./application/source/io/keyboard.d ./application/source/io/keyboard.o ./application/source/io/keyboard.su

.PHONY: clean-application-2f-source-2f-io

