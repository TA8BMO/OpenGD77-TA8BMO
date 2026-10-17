################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/functions/aprs.c \
../application/source/functions/fiveToneDecode.c \
../application/source/functions/dtmfDecode.c \
../application/source/functions/callLog.c \
../application/source/functions/voiceRecorder.c \
../application/source/functions/toneFinder.c \
../application/source/functions/morseDecode.c \
../application/source/functions/aprsDecode.c \
../application/source/functions/calibration.c \
../application/source/functions/codeplug.c \
../application/source/functions/hotspot.c \
../application/source/functions/rxPowerSaving.c \
../application/source/functions/satellite.c \
../application/source/functions/settings.c \
../application/source/functions/sound.c \
../application/source/functions/ticks.c \
../application/source/functions/trx.c \
../application/source/functions/voicePrompts.c \
../application/source/functions/vox.c 

OBJS += \
./application/source/functions/aprs.o \
./application/source/functions/fiveToneDecode.o \
./application/source/functions/dtmfDecode.o \
./application/source/functions/callLog.o \
./application/source/functions/voiceRecorder.o \
./application/source/functions/toneFinder.o \
./application/source/functions/morseDecode.o \
./application/source/functions/aprsDecode.o \
./application/source/functions/calibration.o \
./application/source/functions/codeplug.o \
./application/source/functions/hotspot.o \
./application/source/functions/rxPowerSaving.o \
./application/source/functions/satellite.o \
./application/source/functions/settings.o \
./application/source/functions/sound.o \
./application/source/functions/ticks.o \
./application/source/functions/trx.o \
./application/source/functions/voicePrompts.o \
./application/source/functions/vox.o 

C_DEPS += \
./application/source/functions/aprs.d \
./application/source/functions/fiveToneDecode.d \
./application/source/functions/dtmfDecode.d \
./application/source/functions/callLog.d \
./application/source/functions/voiceRecorder.d \
./application/source/functions/toneFinder.d \
./application/source/functions/morseDecode.d \
./application/source/functions/aprsDecode.d \
./application/source/functions/calibration.d \
./application/source/functions/codeplug.d \
./application/source/functions/hotspot.d \
./application/source/functions/rxPowerSaving.d \
./application/source/functions/satellite.d \
./application/source/functions/settings.d \
./application/source/functions/sound.d \
./application/source/functions/ticks.d \
./application/source/functions/trx.d \
./application/source/functions/voicePrompts.d \
./application/source/functions/vox.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/functions/%.o application/source/functions/%.su application/source/functions/%.cyclo: ../application/source/functions/%.c application/source/functions/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
application/source/functions/hotspot.o: ../application/source/functions/hotspot.c application/source/functions/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -DGITVERSION=`git rev-parse --short HEAD || echo UNKNOWN` -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-functions

clean-application-2f-source-2f-functions:
	-$(RM) ./application/source/functions/aprs.cyclo ./application/source/functions/aprs.d ./application/source/functions/aprs.o ./application/source/functions/aprs.su ./application/source/functions/calibration.cyclo ./application/source/functions/calibration.d ./application/source/functions/calibration.o ./application/source/functions/calibration.su ./application/source/functions/codeplug.cyclo ./application/source/functions/codeplug.d ./application/source/functions/codeplug.o ./application/source/functions/codeplug.su ./application/source/functions/hotspot.cyclo ./application/source/functions/hotspot.d ./application/source/functions/hotspot.o ./application/source/functions/hotspot.su ./application/source/functions/rxPowerSaving.cyclo ./application/source/functions/rxPowerSaving.d ./application/source/functions/rxPowerSaving.o ./application/source/functions/rxPowerSaving.su ./application/source/functions/satellite.cyclo ./application/source/functions/satellite.d ./application/source/functions/satellite.o ./application/source/functions/satellite.su ./application/source/functions/settings.cyclo ./application/source/functions/settings.d ./application/source/functions/settings.o ./application/source/functions/settings.su ./application/source/functions/sound.cyclo ./application/source/functions/sound.d ./application/source/functions/sound.o ./application/source/functions/sound.su ./application/source/functions/ticks.cyclo ./application/source/functions/ticks.d ./application/source/functions/ticks.o ./application/source/functions/ticks.su ./application/source/functions/trx.cyclo ./application/source/functions/trx.d ./application/source/functions/trx.o ./application/source/functions/trx.su ./application/source/functions/voicePrompts.cyclo ./application/source/functions/voicePrompts.d ./application/source/functions/voicePrompts.o ./application/source/functions/voicePrompts.su ./application/source/functions/vox.cyclo ./application/source/functions/vox.d ./application/source/functions/vox.o ./application/source/functions/vox.su

.PHONY: clean-application-2f-source-2f-functions

