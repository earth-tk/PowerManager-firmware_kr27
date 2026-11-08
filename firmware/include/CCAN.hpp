#pragma once

#include "fdcan.h"

#define CAN_RX_BUFF_N 16
#define CAN_RX_BUFF_AND 0xF

struct CANFD_Frame{
	uint32_t id=0;
	uint8_t data[64]={0};
	size_t size=0;
	bool  is_free = true;
	bool is_remote = false;
};

class CANFD{
private:
	FDCAN_HandleTypeDef *fdcan_;

	CANFD_Frame rx_buff[CAN_RX_BUFF_N];
	uint8_t		TxData[64];
	uint32_t head = 0;
	uint32_t tail = 0;
	FDCAN_FilterTypeDef filter_;

public:
	CANFD(FDCAN_HandleTypeDef *can) : fdcan_(can)
	{
	}
	//can tx CANFD_Frame/////////////////////////////
	void start();
	bool tx(CANFD_Frame &tx_data);

	//can rx fuctions//////////////////////////////
	uint32_t rx_available(void);
	void rx_interrupt_task(void);
	bool rx(CANFD_Frame &rx_frame);

	//can filter setting///////////////////////////
	void set_filter_mask(uint8_t index, uint32_t id,uint32_t mask);
	void set_filter_free(void);

	static inline uint16_t len2dlc(uint16_t byte) {
    	return (byte <= 8) ? byte :
           (byte <= 24) ? (byte - 8 + 3) / 4 + 8 :
           (byte <= 32) ? (byte - 24 + 7) / 8 + 12 :
           (byte - 32 + 15) / 16 + 13;
	}
	
	uint8_t dlc2len(uint32_t dlc)
	{
    	static const uint8_t dlc_to_len[16] = {0,1,2,3,4,5,6,7,9,12,16, 20,24,32,48,64};
    	if (dlc > 15) return 0;
    	return dlc_to_len[dlc];
	}
};