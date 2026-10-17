################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/hardware/AT1846S.c \
../application/source/hardware/EEPROM.c \
../application/source/hardware/HR-C6000.c \
../application/source/hardware/HX8353E_display.c \
../application/source/hardware/SPI_Flash.c \
../application/source/hardware/radioHardwareInterface.c 

OBJS += \
./application/source/hardware/AT1846S.o \
./application/source/hardware/EEPROM.o \
./application/source/hardware/HR-C6000.o \
./application/source/hardware/HX8353E_display.o \
./application/source/hardware/SPI_Flash.o \
./application/source/hardware/radioHardwareInterface.o 

C_DEPS += \
./application/source/hardware/AT1846S.d \
./application/source/hardware/EEPROM.d \
./application/source/hardware/HR-C6000.d \
./application/source/hardware/HX8353E_display.d \
./application/source/hardware/SPI_Flash.d \
./application/source/hardware/radioHardwareInterface.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/hardware/%.o application/source/hardware/%.su application/source/hardware/%.cyclo: ../application/source/hardware/%.c application/source/hardware/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-hardware

clean-application-2f-source-2f-hardware:
	-$(RM) ./application/source/hardware/AT1846S.cyclo ./application/source/hardware/AT1846S.d ./application/source/hardware/AT1846S.o ./application/source/hardware/AT1846S.su ./application/source/hardware/EEPROM.cyclo ./application/source/hardware/EEPROM.d ./application/source/hardware/EEPROM.o ./application/source/hardware/EEPROM.su ./application/source/hardware/HR-C6000.cyclo ./application/source/hardware/HR-C6000.d ./application/source/hardware/HR-C6000.o ./application/source/hardware/HR-C6000.su ./application/source/hardware/HX8353E_display.cyclo ./application/source/hardware/HX8353E_display.d ./application/source/hardware/HX8353E_display.o ./application/source/hardware/HX8353E_display.su ./application/source/hardware/SPI_Flash.cyclo ./application/source/hardware/SPI_Flash.d ./application/source/hardware/SPI_Flash.o ./application/source/hardware/SPI_Flash.su ./application/source/hardware/radioHardwareInterface.cyclo ./application/source/hardware/radioHardwareInterface.d ./application/source/hardware/radioHardwareInterface.o ./application/source/hardware/radioHardwareInterface.su

.PHONY: clean-application-2f-source-2f-hardware

