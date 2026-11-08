#include <stdint.h>
#include <string.h>

#include "CANFD.hpp"
#include "FullColorLED.hpp"
#include "fdcan.h"

void CANFD::start(){
	if (HAL_FDCAN_ConfigGlobalFilter(fdcan_, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT) != HAL_OK)
	{
		Error_Handler();
	}
	if(HAL_FDCAN_Start(fdcan_)!= HAL_OK) {
		Error_Handler();
	}
	if (HAL_FDCAN_ActivateNotification(fdcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
	   /* Notification Error */
	   Error_Handler();
	}
}

bool CANFD::tx(CANFD_Frame &tx_data){
	FDCAN_TxHeaderTypeDef	TxHeader;
	TxHeader.Identifier = tx_data.id;
	TxHeader.IdType = FDCAN_STANDARD_ID;
	TxHeader.TxFrameType = tx_data.is_remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
	TxHeader.DataLength = len2dlc(tx_data.size);
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_ON;
	TxHeader.FDFormat = FDCAN_FD_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	// Prepare first Tx data for fdcan1
	for (size_t i = 0; i < tx_data.size; i++) {
		TxData[i] = tx_data.data[i];
	}

	// Put Tx data to Txfifo
	if (HAL_FDCAN_AddMessageToTxFifoQ(fdcan_, &TxHeader, TxData)!= HAL_OK) {
		Error_Handler();
	}
	return true;
}

uint32_t CANFD::rx_available(void){
	uint32_t count = 0;
	for(uint32_t i = 0; i < CAN_RX_BUFF_N;i++){
		if(!rx_buff[i].is_free) count ++;
	}
	return count;
}

void CANFD::rx_interrupt_task(void){
	FDCAN_RxHeaderTypeDef	RxHeader;
	uint8_t		fdcan1RxData[64];

    if (HAL_FDCAN_GetRxMessage(fdcan_, FDCAN_RX_FIFO0, &RxHeader, fdcan1RxData) != HAL_OK) {
		Error_Handler();
    }

    if (HAL_FDCAN_ActivateNotification(fdcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
          /* Notification Error */
    	Error_Handler();
    }
	
	rx_buff[head].id = RxHeader.Identifier;
	rx_buff[head].size = dlc2len(RxHeader.DataLength);
 	memcpy(&rx_buff[head].data, fdcan1RxData, 64);
	rx_buff[head].is_free = false;
	rx_buff[head].is_remote = RxHeader.RxFrameType == FDCAN_REMOTE_FRAME;

	head = (head+1)&CAN_RX_BUFF_AND;
}

bool CANFD::rx(CANFD_Frame &rx_frame){
	if(!rx_buff[tail].is_free){
		rx_frame = rx_buff[tail];
		rx_buff[tail].is_free = true;
		tail = (tail+1)&CAN_RX_BUFF_AND;
		return true;
	}else{
		return false;
	}
}

void CANFD::set_filter_mask(uint8_t index, uint32_t id,uint32_t mask){
	filter_.IdType = FDCAN_STANDARD_ID;
	filter_.FilterIndex = index;
	filter_.FilterType = FDCAN_FILTER_MASK;
	filter_.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	filter_.FilterID1 = id;
	filter_.FilterID2 = mask;

	if (HAL_FDCAN_ConfigFilter(fdcan_, &filter_) != HAL_OK)
	{
		Error_Handler();
	}
}
