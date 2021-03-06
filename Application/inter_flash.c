#include <stdint.h>
#include <string.h>

#include "bsp.h"
#include "pstorage.h"
#include "app_error.h"

#include "inter_flash.h"
#include "ble_init.h"
#include "set_params.h"

char	super_key[SUPER_KEY_LENGTH];

pstorage_handle_t		block_id_flash_store;

pstorage_handle_t		block_id_default_params;
pstorage_handle_t		block_id_mac;
pstorage_handle_t		block_id_super_key;
pstorage_handle_t		block_id_seed;
pstorage_handle_t		block_id_device_name;
pstorage_handle_t		block_id_key_store;
pstorage_handle_t		block_id_record;
pstorage_handle_t		block_id_fig_info;

struct record_length_struct			record_length;

struct key_store_struct				key_store_struct_set;
struct door_open_record				door_open_record_get;

struct fig_info	fig_info_set; //设置的指纹信息
struct fig_info	fig_info_get; //获取的指纹信息

uint8_t					interflash_write_data[BLOCK_STORE_SIZE];
uint8_t					interflash_read_data[BLOCK_STORE_SIZE];

pstorage_handle_t		block_id_write;
pstorage_handle_t		block_id_read;

uint8_t		flash_write_data[BLOCK_STORE_SIZE];
uint8_t		flash_read_data[BLOCK_STORE_SIZE];
uint8_t		flash_read_key_store_data[BLOCK_STORE_SIZE];
uint8_t		flash_write_key_store_data[BLOCK_STORE_SIZE];
uint8_t		flash_read_record_data[BLOCK_STORE_SIZE];
uint8_t		flash_write_record_data[BLOCK_STORE_SIZE];

uint8_t		flash_read_temp[BLOCK_STORE_SIZE];

/************************************************************************
*flash操作的回调函数
*in:	*handle		操作的flash的block_id
		op_code		操作码，具体定义参照pstorage.h
		result		执行结果
		*p_data		操作的数据的指针
		len			数据的长度
************************************************************************/
static void my_cb(pstorage_block_t *handle, uint8_t op_code, \
                  uint32_t result, uint8_t *p_data, uint32_t data_len) {
	switch(op_code) {
	case PSTORAGE_LOAD_OP_CODE:
		if (result != NRF_SUCCESS) {
			printf("pstorage LOAD ERROR callback received \r\n");
		}
		break;
	case PSTORAGE_STORE_OP_CODE:
		if (result != NRF_SUCCESS) {
			printf("pstorage STORE ERROR callback received \r\n");
		}
		break;
	case PSTORAGE_UPDATE_OP_CODE:
		if (result != NRF_SUCCESS) {
			printf("pstorage UPDATE ERROR callback received \r\n");
		}
		break;
	case PSTORAGE_CLEAR_OP_CODE:
		if (result != NRF_SUCCESS) {
			printf("pstorage CLEAR ERROR callback received \r\n");
		}
		break;
	default:
		break;
	}

}

/*******************************************
*初始化内部flash空间
*in：		none
*******************************************/
void flash_init(void) {
	uint32_t err_code;

	//	pstorage_init(); //初始化flash操作,在device_manager_init中初始化了，这里就不用了

	//初始化key_store的空间
	pstorage_module_param_t module_param_key_store;
	module_param_key_store.block_count = BLOCK_STORE_COUNT;//申请 BLOCK_STORE_COUNT 个块
	module_param_key_store.block_size = BLOCK_STORE_SIZE; //每块大小 BLOCK_STORE_SIZE bytes
	module_param_key_store.cb = (pstorage_ntf_cb_t)my_cb;

	err_code = pstorage_register(&module_param_key_store, &block_id_flash_store);
	APP_ERROR_CHECK(err_code);
#if defined(BLE_DOOR_DEBUG)
	printf("flash name:block_id_flash_store.\r\n");
	printf("it has %i blocks and block size is %i \r\n",\
	       module_param_key_store.block_count, module_param_key_store.block_size);
#endif

	//取设置的mac
	err_code =pstorage_block_identifier_get(&block_id_flash_store, \
	                                        (pstorage_size_t)MAC_OFFSET, &block_id_mac);
	APP_ERROR_CHECK(err_code);
	err_code = pstorage_load(mac, &block_id_mac, 8, 0);
	APP_ERROR_CHECK(err_code);

	//取设置的device_name
	/*	err_code =pstorage_block_identifier_get(&block_id_flash_store, \
							(pstorage_size_t)DEVICE_NAME_OFFSET, &block_id_device_name);
		APP_ERROR_CHECK(err_code);
		err_code = pstorage_load(device_name,&block_id_device_name,DEVICE_NAME_SIZE,0);
		APP_ERROR_CHECK(err_code);
	*/
	//如果开门记录的条数为全f，写开门记录条数为0
	err_code = pstorage_block_identifier_get(&block_id_flash_store, \
	           (pstorage_size_t)RECORD_OFFSET, &block_id_record);
	APP_ERROR_CHECK(err_code);
	err_code = pstorage_load((uint8_t *)&record_length, &block_id_record, sizeof(struct record_length_struct), 0);
	if(err_code == NRF_SUCCESS) {
		if(record_length.record_length == 0xffffffff) {
			record_length.record_length = 0x0;
			record_length.record_full = 0x0;
			err_code = pstorage_clear(&block_id_record,BLOCK_STORE_SIZE);
			APP_ERROR_CHECK(err_code);
			err_code = pstorage_store(&block_id_record, (uint8_t *)&record_length, sizeof(struct record_length_struct), 0);
			APP_ERROR_CHECK(err_code);
		}
#if defined(BLE_DOOR_DEBUG)
		printf("record length set %d\r\n", record_length.record_length);
#endif
	}
#if defined(BLE_DOOR_DEBUG)
	printf("flash init success \r\n");
#endif

}


/**********************************************************
*存储到flash
*in：	*p_data		写入数据的指针
		data_len		数据长度
		block_id_offset		写入的block偏移量
		*block_di_write		写入的block_id
**********************************************************/
int interflash_write(uint8_t *p_data, uint32_t data_len,\
                     pstorage_size_t block_id_offset ) {
	uint32_t err_code;
	uint8_t data_len_4_left;
	uint32_t data_len_write;
	if(data_len <= BLOCK_STORE_SIZE) {
		//1、判断数据的长度是否是4的倍数，获取要写入的实际字节数
		data_len_4_left = data_len%4;
		if(data_len_4_left ==0) {
			//字节数是4的倍数
			data_len_write = data_len;
		} else {
			//字节数不是4的倍数
			data_len_write = (data_len +4 - data_len_4_left);
		}

		//2、组织要写入的数据数组
		memset(interflash_write_data, 0, BLOCK_STORE_SIZE);
		memcpy(interflash_write_data, p_data, data_len);

		//3、将数据存储到指定的位置
		//获取需要存储的位置
		pstorage_block_identifier_get(&block_id_flash_store, block_id_offset, &block_id_write);
		//清除当前存储区域
		pstorage_clear(&block_id_write, BLOCK_STORE_SIZE);
		err_code = pstorage_store(&block_id_write, interflash_write_data, (pstorage_size_t)data_len_write, 0);
		if(err_code ==NRF_SUCCESS) {
#if defined(BLE_DOOR_DEBUG)
			printf("%2d bytes store in flash offset:%i\r\n", data_len, block_id_offset);
#endif
		}
		return 0;
	} else {
		return 1;
	}

}

/**********************************************************
*将flash中的数据读出来
*in：		*p_data		写入数据的指针
			data_len		数据长度
			block_id_offset		写入的block偏移量
			*block_di_read		写入的block_id
**********************************************************/
int interflash_read(uint8_t *p_data, uint32_t data_len, \
                    pstorage_size_t block_id_offset) {
	uint32_t err_code;
	uint8_t data_len_4_left;
	uint32_t data_len_read;
	if(data_len <= BLOCK_STORE_SIZE) {
		//1、判断数据的长度是否是4的倍数，获取要写入的实际字节数
		data_len_4_left = data_len%4;
		if(data_len_4_left ==0) {
			//字节数是4的倍数
			data_len_read = data_len;
		} else {
			//字节数不是4的倍数
			data_len_read = (data_len +4 - data_len_4_left);
		}
		memset(interflash_read_data, 0, BLOCK_STORE_SIZE);
		pstorage_block_identifier_get(&block_id_flash_store, (pstorage_size_t)block_id_offset, &block_id_read);
		err_code = pstorage_load(interflash_read_data, &block_id_read, (pstorage_size_t)data_len_read, 0);
		if(err_code ==NRF_SUCCESS) {
			memset(p_data, 0, data_len);
			memcpy(p_data, interflash_read_data, data_len);
#ifdef BLE_DOOR_DEBUG
			printf("%2d bytes read in flash offset:%i\r\n", data_len, block_id_offset);
#endif
		}
		return 0;
	} else {
		return 1;
	}

}

/*************************************************************************
*写入管理员秘钥(12位ASCII)
*in：		*p_data			超级密码的指针
				data_len			数据的长度
************************************************************************/
int write_super_key(uint8_t *p_data, uint32_t data_len) {
	uint8_t data_len_4_left;
	uint32_t data_len_write;
	if(data_len <= BLOCK_STORE_SIZE) {
		//1、判断数据的长度是否是4的倍数，获取要写入的实际字节数
		data_len_4_left = data_len%4;
		if(data_len_4_left ==0) {
			//字节数是4的倍数
			data_len_write = data_len;
		} else {
			//字节数不是4的倍数
			data_len_write = (data_len +4 - data_len_4_left);
		}
		//2、组织要写入的数据数组
		memset(interflash_write_data, 0, BLOCK_STORE_SIZE);
		memcpy(interflash_write_data, p_data, data_len);

		//3、将数据存储到指定的位置
		pstorage_block_identifier_get(&block_id_flash_store, (pstorage_size_t)SUPER_KEY_OFFSET, &block_id_super_key);
		pstorage_clear(&block_id_super_key,BLOCK_STORE_SIZE);
		pstorage_store(&block_id_super_key, interflash_write_data, data_len_write, 0);
#ifdef BLE_DOOR_DEBUG
		printf("super key write:");
		for(int i=0; i<SUPER_KEY_LENGTH; i++) {
			//第一位是'w'
			printf("%c ",p_data[i+1]);
		}
		printf("\r\n");
#endif
		return 0;
	} else {
		return 1;
	}

}

/***********************************************************************
*将钥匙存储在flash中
*in：		*key_store_input			写入的密码结构体指针
*key_store_input->control_bits =0x01,以前的记录清除
*记录达到上限KEY_STORE_NUMBER，则循环记录
**********************************************************************/
void key_store_write(struct key_store_struct *key_store_input, uint16_t key_store_site) {

	memset(flash_write_key_store_data, 0, BLOCK_STORE_SIZE);
	memcpy(flash_write_key_store_data,key_store_input, sizeof(struct key_store_struct));
	interflash_write(flash_write_key_store_data, BLOCK_STORE_SIZE, \
	                 (pstorage_size_t)(KEY_STORE_OFFSET + key_store_site));

#if defined(BLE_DOOR_DEBUG)
	printf("key set  success:");
	for(int j=0; j<KEY_LENGTH; j++) {
		printf("%c",key_store_input->key_store[j]);
	}
	printf("\r\n");
#endif

}

/********************************************************************
*将记录存储在flash中
*in：			*open_record			写入的记录结构体
*********************************************************************/
void record_write(struct door_open_record *open_record) {
	//写记录条数
	pstorage_block_identifier_get(&block_id_flash_store, (pstorage_size_t)RECORD_OFFSET, &block_id_record);
	pstorage_load((uint8_t *)&record_length, &block_id_record, sizeof(struct record_length_struct), 0);
	if(record_length.record_length >= RECORD_NUMBER) {
		//达到记录上限
		record_length.record_length = 0x1;
		record_length.record_full= 0x01;
	} else {
		//未达到记录上限
		record_length.record_length++;
	}
	pstorage_clear(&block_id_record, BLOCK_STORE_SIZE);
	pstorage_store(&block_id_record, (uint8_t *)&record_length, 4, 0);

	memset(flash_write_record_data, 0, BLOCK_STORE_SIZE);
	memcpy(flash_write_record_data, open_record, sizeof(struct door_open_record));
	interflash_write(flash_write_record_data, BLOCK_STORE_SIZE,\
	                 (pstorage_size_t)(RECORD_OFFSET + record_length.record_length));

}

/************************************************
*将指纹信息存储到内部flash
************************************************/
int fig_info_write(struct fig_info *fp_info_set_p) {
	int err_code;
//	err_code = interflash_write((uint8_t *) fp_info_set_p, sizeof(struct fig_info), \
	(pstorage_size_t)(FIG_INFO_OFFSET + fp_info_set_p->fig_info_id));
	//1、获取label
	pstorage_block_identifier_get(&block_id_flash_store, \
	                              (pstorage_size_t)(FIG_INFO_OFFSET+fp_info_set_p->fig_info_id), &block_id_fig_info);
	//2、清除区域
	pstorage_clear(&block_id_fig_info, BLOCK_STORE_SIZE);
	//3、存储信息
	memset(interflash_write_data, 0, BLOCK_STORE_SIZE);
	memcpy(interflash_write_data, fp_info_set_p, sizeof(struct fig_info));
	err_code = pstorage_store(&block_id_fig_info, interflash_write_data, BLOCK_STORE_SIZE, 0);

	return err_code;
}

/*************************************************
*读取内部flash中指定位置的
*************************************************/
int fig_info_read(struct fig_info *fp_info_get_p) {
	int err_code;
	err_code = interflash_write((uint8_t *) fp_info_get_p, sizeof(struct fig_info), \
	                            (pstorage_size_t)(FIG_INFO_OFFSET + fp_info_get_p->fig_info_id));
	return err_code;
}
