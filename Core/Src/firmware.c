#include <math.h>

#include "main.h"

////////////////////////////////////////////////////////////////////////////////

#define FIRMWARE_VERSION_MAJOR 0x00
#define FIRMWARE_VERSION_MINOR 0x03

////////////////////////////////////////////////////////////////////////////////

extern SPI_HandleTypeDef hspi1;
extern CRC_HandleTypeDef hcrc;

////////////////////////////////////////////////////////////////////////////////

#define BUFFER_SIZE 20

uint8_t output[BUFFER_SIZE];
uint8_t input[BUFFER_SIZE];

uint32_t *output_CRC = (uint32_t *)(output + BUFFER_SIZE - 4);

int transferCompleted;

int loopOperationIndex;

////////////////////////////////////////////////////////////////////////////////

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
}

////////////////////////////////////////////////////////////////////////////////

#define SPI_SLAVE_SYNBYTE         0x53
#define SPI_MASTER_SYNBYTE        0xAC

void slaveSynchro(void) {
    uint32_t idw0 = HAL_GetUIDw0();
    uint32_t idw1 = HAL_GetUIDw1();
    uint32_t idw2 = HAL_GetUIDw2();

    uint8_t txBuffer[15] = {
        SPI_SLAVE_SYNBYTE,
        FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR,
        idw0 >> 24, (idw0 >> 16) & 0xFF, (idw0 >> 8) & 0xFF, idw0 & 0xFF,
        idw1 >> 24, (idw1 >> 16) & 0xFF, (idw1 >> 8) & 0xFF, idw1 & 0xFF,
        idw2 >> 24, (idw2 >> 16) & 0xFF, (idw2 >> 8) & 0xFF, idw2 & 0xFF
    };

    uint8_t rxBuffer[15];

    while (1) {
		rxBuffer[0] = 0;

		transferCompleted = 0;
        if (HAL_SPI_TransmitReceive_DMA(&hspi1, (uint8_t *)&txBuffer, (uint8_t *)&rxBuffer, sizeof(rxBuffer)) != HAL_OK) {
        	continue;
        }
		while (!transferCompleted);

//		if (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)&txBuffer, (uint8_t *)&rxBuffer, sizeof(rxBuffer), HAL_MAX_DELAY) != HAL_OK) {
//			continue;
//		}

		if (rxBuffer[0] == SPI_MASTER_SYNBYTE) {
			break;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////

void loopOperation_Dummy() {
}

typedef void (*LoopOperation)();

LoopOperation loopOperations[] = {
	loopOperation_Dummy
};

static const int NUM_LOOP_OPERATIONS = sizeof(loopOperations) / sizeof(LoopOperation);

////////////////////////////////////////////////////////////////////////////////

void beginTransfer() {
	for (int i = 0; i < BUFFER_SIZE; i++) {
    	output[i] = i;
    }

    *output_CRC = HAL_CRC_Calculate(&hcrc, (uint32_t *)output, BUFFER_SIZE - 4);

    HAL_SPI_TransmitReceive_DMA(&hspi1, output, input, BUFFER_SIZE);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
	transferCompleted = 1;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
	transferCompleted = 1;
}

void setup() {
    slaveSynchro();

    beginTransfer();
    HAL_GPIO_WritePin(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin, GPIO_PIN_SET);
}

void loop() {
    if (transferCompleted) {
        transferCompleted = 0;

        HAL_Delay(1);

        beginTransfer();
    }

    LoopOperation loopOperation = loopOperations[loopOperationIndex];
    loopOperationIndex = (loopOperationIndex + 1) % NUM_LOOP_OPERATIONS;
    loopOperation();
}

