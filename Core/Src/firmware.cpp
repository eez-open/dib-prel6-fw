#include <math.h>
#include <memory.h>
#include <stdlib.h>

#include "main.h"

#include "firmware.h"
#include "utils.h"

////////////////////////////////////////////////////////////////////////////////

SetParams currentState;

////////////////////////////////////////////////////////////////////////////////

static const int NUM_RELAYS = 6;

static GPIO_TypeDef *RELAY_PORTS[NUM_RELAYS] = {
	PR0_GPIO_Port, PR1_GPIO_Port, PR2_GPIO_Port,
	PR3_GPIO_Port, PR4_GPIO_Port, PR5_GPIO_Port
};

static uint16_t RELAY_PINS[NUM_RELAYS] = {
	PR0_Pin, PR1_Pin, PR2_Pin,
	PR3_Pin, PR4_Pin, PR5_Pin
};

void Relays_Setup() {
	for (int i = 0; i < NUM_RELAYS; i++) {
		RESET_PIN(RELAY_PORTS[i], RELAY_PINS[i]);
	}

	HAL_Delay(10); // prevent debounce
}

void Relays_Update(SetParams &newState) {
	for (int i = 0; i < NUM_RELAYS; i++) {
		auto relayState = newState.relayStates & (1 << i);
		if ((currentState.relayStates & (1 << i)) != relayState) {
			if (relayState) {
				SET_PIN(RELAY_PORTS[i], RELAY_PINS[i]);
			} else {
				RESET_PIN(RELAY_PORTS[i], RELAY_PINS[i]);
			}
		}
	}

	HAL_Delay(10); // prevent debounce
}

////////////////////////////////////////////////////////////////////////////////
// MASTER - SLAVE Communication

extern SPI_HandleTypeDef hspi1;
extern "C" void SPI1_Init(void);
SPI_HandleTypeDef *hspiMaster = &hspi1; // for MASTER-SLAVE communication

#define BUFFER_SIZE 20

uint32_t input[(BUFFER_SIZE + 3) / 4 + 1];
uint32_t output[(BUFFER_SIZE + 3) / 4];
volatile int transferCompleted;

void beginTransfer() {
    transferCompleted = 0;
    HAL_SPI_TransmitReceive_DMA(hspiMaster, (uint8_t *)output, (uint8_t *)input, BUFFER_SIZE);
    RESET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
	if (hspi == hspiMaster) {
		SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
		transferCompleted = 1;
	}
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
	if (hspi == hspiMaster) {
		SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
		transferCompleted = 2;
	}
}

int waitTransferCompletion() {
	uint32_t startTick = HAL_GetTick();
	while (!transferCompleted) {
		if (HAL_GetTick() - startTick > 500) {
			HAL_SPI_Abort(hspiMaster);
			SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
			return 2;
		}
	}
	return transferCompleted;
}

////////////////////////////////////////////////////////////////////////////////

void Command_GetInfo(Request &request, Response &response) {
	response.getInfo.firmwareMajorVersion = FIRMWARE_VERSION_MAJOR;
	response.getInfo.firmwareMinorVersion = FIRMWARE_VERSION_MINOR;
	response.getInfo.idw0 = HAL_GetUIDw0();
	response.getInfo.idw1 = HAL_GetUIDw1();
	response.getInfo.idw2 = HAL_GetUIDw2();
}

void Command_GetState(Request &request, Response &response) {
	response.getState.tickCount = HAL_GetTick();
}

void Command_SetParams(Request &request, Response &response) {
	Relays_Update(request.setParams);
	memcpy(&currentState, &request.setParams, sizeof(SetParams));
	response.setParams.result = 1; // success
}

////////////////////////////////////////////////////////////////////////////////

extern "C" void setup() {
	Relays_Setup();
}

extern "C" void loop() {
	beginTransfer();
    auto transferResult = waitTransferCompletion();

    Request &request = *(Request *)input;
    Response &response = *(Response *)output;

    if (transferResult == 1) {
    	response.command = 0x8000 | request.command;

		if (request.command == COMMAND_GET_INFO) {
			Command_GetInfo(request, response);
		} else if (request.command == COMMAND_GET_STATE) {
			Command_GetState(request, response);
		} else if (request.command == COMMAND_SET_PARAMS) {
			Command_SetParams(request, response);
		} else {
			response.command = COMMAND_NONE;
		}
    } else {
		response.command = COMMAND_NONE;

    	HAL_SPI_DeInit(hspiMaster);
		SPI1_Init();
    }
}
