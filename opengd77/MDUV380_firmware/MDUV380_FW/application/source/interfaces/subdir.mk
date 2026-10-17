################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/interfaces/adc.c \
../application/source/interfaces/batteryAndPowerManagement.c \
../application/source/interfaces/batteryRAM.c \
../application/source/interfaces/clockManager.c \
../application/source/interfaces/dac.c \
../application/source/interfaces/gpio.c \
../application/source/interfaces/gps.c \
../application/source/interfaces/i2c.c \
../application/source/interfaces/i2s.c \
../application/source/interfaces/interrupts.c \
../application/source/interfaces/pit.c \
../application/source/interfaces/settingsStorage.c \
../application/source/interfaces/spi.c \
../application/source/interfaces/stm32f4xx_ll_pwr.c \
../application/source/interfaces/wdog.c 

OBJS += \
./application/source/interfaces/adc.o \
./application/source/interfaces/batteryAndPowerManagement.o \
./application/source/interfaces/batteryRAM.o \
./application/source/interfaces/clockManager.o \
./application/source/interfaces/dac.o \
./application/source/interfaces/gpio.o \
./application/source/interfaces/gps.o \
./application/source/interfaces/i2c.o \
./application/source/interfaces/i2s.o \
./application/source/interfaces/interrupts.o \
./application/source/interfaces/pit.o \
./application/source/interfaces/settingsStorage.o \
./application/source/interfaces/spi.o \
./application/source/interfaces/stm32f4xx_ll_pwr.o \
./application/source/interfaces/wdog.o 

C_DEPS += \
./application/source/interfaces/adc.d \
./application/source/interfaces/batteryAndPowerManagement.d \
./application/source/interfaces/batteryRAM.d \
./application/source/interfaces/clockManager.d \
./application/source/interfaces/dac.d \
./application/source/interfaces/gpio.d \
./application/source/interfaces/gps.d \
./application/source/interfaces/i2c.d \
./application/source/interfaces/i2s.d \
./application/source/interfaces/interrupts.d \
./application/source/interfaces/pit.d \
./application/source/interfaces/settingsStorage.d \
./application/source/interfaces/spi.d \
./application/source/interfaces/stm32f4xx_ll_pwr.d \
./application/source/interfaces/wdog.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/interfaces/%.o application/source/interfaces/%.su application/source/interfaces/%.cyclo: ../application/source/interfaces/%.c application/source/interfaces/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-interfaces

clean-application-2f-source-2f-interfaces:
	-$(RM) ./application/source/interfaces/adc.cyclo ./application/source/interfaces/adc.d ./application/source/interfaces/adc.o ./application/source/interfaces/adc.su ./application/source/interfaces/batteryAndPowerManagement.cyclo ./application/source/interfaces/batteryAndPowerManagement.d ./application/source/interfaces/batteryAndPowerManagement.o ./application/source/interfaces/batteryAndPowerManagement.su ./application/source/interfaces/batteryRAM.cyclo ./application/source/interfaces/batteryRAM.d ./application/source/interfaces/batteryRAM.o ./application/source/interfaces/batteryRAM.su ./application/source/interfaces/clockManager.cyclo ./application/source/interfaces/clockManager.d ./application/source/interfaces/clockManager.o ./application/source/interfaces/clockManager.su ./application/source/interfaces/dac.cyclo ./application/source/interfaces/dac.d ./application/source/interfaces/dac.o ./application/source/interfaces/dac.su ./application/source/interfaces/gpio.cyclo ./application/source/interfaces/gpio.d ./application/source/interfaces/gpio.o ./application/source/interfaces/gpio.su ./application/source/interfaces/gps.cyclo ./application/source/interfaces/gps.d ./application/source/interfaces/gps.o ./application/source/interfaces/gps.su ./application/source/interfaces/i2c.cyclo ./application/source/interfaces/i2c.d ./application/source/interfaces/i2c.o ./application/source/interfaces/i2c.su ./application/source/interfaces/i2s.cyclo ./application/source/interfaces/i2s.d ./application/source/interfaces/i2s.o ./application/source/interfaces/i2s.su ./application/source/interfaces/interrupts.cyclo ./application/source/interfaces/interrupts.d ./application/source/interfaces/interrupts.o ./application/source/interfaces/interrupts.su ./application/source/interfaces/pit.cyclo ./application/source/interfaces/pit.d ./application/source/interfaces/pit.o ./application/source/interfaces/pit.su ./application/source/interfaces/settingsStorage.cyclo ./application/source/interfaces/settingsStorage.d ./application/source/interfaces/settingsStorage.o ./application/source/interfaces/settingsStorage.su ./application/source/interfaces/spi.cyclo ./application/source/interfaces/spi.d ./application/source/interfaces/spi.o ./application/source/interfaces/spi.su ./application/source/interfaces/stm32f4xx_ll_pwr.cyclo ./application/source/interfaces/stm32f4xx_ll_pwr.d ./application/source/interfaces/stm32f4xx_ll_pwr.o ./application/source/interfaces/stm32f4xx_ll_pwr.su ./application/source/interfaces/wdog.cyclo ./application/source/interfaces/wdog.d ./application/source/interfaces/wdog.o ./application/source/interfaces/wdog.su

.PHONY: clean-application-2f-source-2f-interfaces

