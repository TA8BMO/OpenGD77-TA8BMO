################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/dmr_codec/codec.c \
../application/source/dmr_codec/codec_interface.c 

S_UPPER_SRCS += \
../application/source/dmr_codec/codec_bin.S 

OBJS += \
./application/source/dmr_codec/codec.o \
./application/source/dmr_codec/codec_bin.o \
./application/source/dmr_codec/codec_interface.o 

S_UPPER_DEPS += \
./application/source/dmr_codec/codec_bin.d 

C_DEPS += \
./application/source/dmr_codec/codec.d \
./application/source/dmr_codec/codec_interface.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/dmr_codec/%.o application/source/dmr_codec/%.su application/source/dmr_codec/%.cyclo: ../application/source/dmr_codec/%.c application/source/dmr_codec/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
application/source/dmr_codec/%.o: ../application/source/dmr_codec/%.S application/source/dmr_codec/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m4 -g3 -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DDEBUG -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"
application/source/dmr_codec/codec_interface.o: ../application/source/dmr_codec/codec_interface.c application/source/dmr_codec/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DPLATFORM_VARIANT_UV380_PLUS_10W -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -O0 -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-dmr_codec

clean-application-2f-source-2f-dmr_codec:
	-$(RM) ./application/source/dmr_codec/codec.cyclo ./application/source/dmr_codec/codec.d ./application/source/dmr_codec/codec.o ./application/source/dmr_codec/codec.su ./application/source/dmr_codec/codec_bin.d ./application/source/dmr_codec/codec_bin.o ./application/source/dmr_codec/codec_interface.cyclo ./application/source/dmr_codec/codec_interface.d ./application/source/dmr_codec/codec_interface.o ./application/source/dmr_codec/codec_interface.su

.PHONY: clean-application-2f-source-2f-dmr_codec

