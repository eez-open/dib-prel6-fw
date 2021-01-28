#include <math.h>
#include <memory.h>
#include <stdlib.h>

#include "main.h"

#include "firmware.h"
#include "utils.h"

//static const uint32_t CONF_SPI_TRANSFER_TIMEOUT_MS = 2000;
static const uint32_t CONF_RELAY_DEBOUNCE_TIME_MS = 10;

// master-slave communication
extern "C" void SPI1_Init(void);
extern SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef *hspiMaster = &hspi1; // SPI for MASTER-SLAVE communication
volatile enum {
	TRANSFER_STATE_WAIT,
	TRANSFER_STATE_SUCCESS,
	TRANSFER_STATE_ERROR
} transferState;

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
	SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
	transferState = TRANSFER_STATE_SUCCESS;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
	SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
	transferState = TRANSFER_STATE_ERROR;
}

// relays
static const int NUM_RELAYS = 6;
static GPIO_TypeDef *RELAY_PORTS[NUM_RELAYS] = { PR0_GPIO_Port, PR1_GPIO_Port, PR2_GPIO_Port, PR3_GPIO_Port, PR4_GPIO_Port, PR5_GPIO_Port };
static uint16_t RELAY_PINS[NUM_RELAYS] = { PR0_Pin, PR1_Pin, PR2_Pin, PR3_Pin, PR4_Pin, PR5_Pin };
uint8_t g_relayStates = 0; // use this to store the latest state of relays, at the beginning all are off

// setup is called once at the beginning from the main.c
extern "C" void setup() {
	// setup relays, i. e. turn all off
	for (int i = 0; i < NUM_RELAYS; i++) {
		RESET_PIN(RELAY_PORTS[i], RELAY_PINS[i]);
	}
	HAL_Delay(CONF_RELAY_DEBOUNCE_TIME_MS); // prevent debounce
}

// loop is called, of course, inside the loop from the main.c
extern "C" void loop() {
	// start SPI transfer
	static const size_t BUFFER_SIZE = 20;
	uint32_t input[(BUFFER_SIZE + 3) / 4 + 1];
	uint32_t output[(BUFFER_SIZE + 3) / 4];
    transferState = TRANSFER_STATE_WAIT;
    HAL_SPI_TransmitReceive_DMA(hspiMaster, (uint8_t *)output, (uint8_t *)input, BUFFER_SIZE);
    RESET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin); // inform master that module is ready for the SPI communication

    // wait for the transfer to finish
//	uint32_t startTick = HAL_GetTick();
	while (transferState == TRANSFER_STATE_WAIT) {
//		if (HAL_GetTick() - startTick > CONF_SPI_TRANSFER_TIMEOUT_MS) {
//			// transfer is taking too long to finish, maybe something is stuck, abort it
//			__disable_irq();
//			HAL_SPI_Abort(hspiMaster);
//			SET_PIN(DIB_IRQ_GPIO_Port, DIB_IRQ_Pin);
//			transferState = TRANSFER_STATE_ERROR;
//			__enable_irq();
//			break;
//		}
	}

	// Request and Response are defined in firmware.h
    Request &request = *(Request *)input;
    Response &response = *(Response *)output;

    if (transferState == TRANSFER_STATE_SUCCESS) {
    	// a way to tell the master that command was handled
    	response.command = 0x8000 | request.command;

		if (request.command == COMMAND_GET_INFO) {
			// return back to the master firmware version and MCU id
			response.getInfo.firmwareMajorVersion = FIRMWARE_VERSION_MAJOR;
			response.getInfo.firmwareMinorVersion = FIRMWARE_VERSION_MINOR;
			response.getInfo.idw0 = HAL_GetUIDw0();
			response.getInfo.idw1 = HAL_GetUIDw1();
			response.getInfo.idw2 = HAL_GetUIDw2();
		}

		else if (request.command == COMMAND_GET_STATE) {
			// master periodically asks for the state of the module,
			// for this module there is nothing much to return really
			response.getState.tickCount = HAL_GetTick();
		}

		else if (request.command == COMMAND_SET_PARAMS) {
			// turn on/off relays as instructed
			for (int i = 0; i < NUM_RELAYS; i++) {
				auto currentRelayState = g_relayStates & (1 << i);
				auto newRelayState = request.setParams.relayStates & (1 << i);
				if (currentRelayState != newRelayState) {
					WRITE_PIN(RELAY_PORTS[i], RELAY_PINS[i], newRelayState);
				}
			}
			g_relayStates = request.setParams.relayStates;
			HAL_Delay(CONF_RELAY_DEBOUNCE_TIME_MS); // prevent debounce

			response.setParams.result = 1; // success
		}

		else {
			// unknown command received, tell the master that no command was handled
			response.command = COMMAND_NONE;
		}
    } else {
    	// invalid transfer, reinitialize SPI just in case
    	HAL_SPI_DeInit(hspiMaster);
		SPI1_Init();

		// tell the master that no command was handled
		response.command = COMMAND_NONE;
    }
}
