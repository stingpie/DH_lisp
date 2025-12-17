/*
 * dollhouse.hpp
 *
 *  Created on: Nov 30, 2025
 *      Author: stingpie
 */

#ifndef DOLLHOUSE_HPP_
#define DOLLHOUSE_HPP_

#include <stdlib.h>
#include <stdint.h>


#define DH_FILENAME_LEN 64

typedef struct Buffer{
	unsigned int size;
	char *data;
} Buffer;



#include "dollhousefile.hpp"



#define DH_DAEMON_NAME_LEN 64
#define DH_FILENAME_LEN 64
#define DH_INTERFACE_NAME_LEN 16
#define DH_LANG_LEN 16
#define DH_TYPE_LEN 16
#define DH_FORMAT_LEN 16
#define DH_ID_LEN 6

struct Message;
struct Daemon;
struct Interface;
struct Interlink;
struct DaemonInfo;


void runDaemon(struct Daemon);
void registerDaemonInterface(struct Interface*);
void *allocateDaemonHeap();
void *allocateDaemonInfoHeap();
void *allocateLispEnvHeap();
int startDaemon(const char*, const char*);

typedef struct Message{ // fits within 256 bytes
	char srcID[DH_ID_LEN], destID[DH_ID_LEN], msgID[DH_ID_LEN]; 	// unique IDs identifying daemons and messages. (2^48 possible values.)
	uint16_t idx;						   	// If more than 180 bytes are required, we send multiple messages.
	uint16_t total;						   	// If more than 180 bytes are required, we send multiple messages. Maxes out at 11 megabytes
	char type[DH_TYPE_LEN], format[DH_FORMAT_LEN], name[DH_INTERFACE_NAME_LEN]; 	// type: basic data type of each value.
											// format: data structure (if applicable)
											// Name: the name of the data being sent.
	char data[256 - (3 * DH_ID_LEN + DH_TYPE_LEN + DH_FORMAT_LEN + DH_INTERFACE_NAME_LEN)];							// actual data being sent.
}Message;


enum DATA_DIRECTION{DATA_OUT, DATA_IN};
typedef struct Interface{
	char name[DH_INTERFACE_NAME_LEN], type[DH_TYPE_LEN], format[DH_FORMAT_LEN];
	uint8_t direction;
	uint8_t triggering;
	struct Daemon *daemon;
}Interface;

typedef struct Daemon{
	char daemonID[DH_ID_LEN], language[DH_LANG_LEN], name[DH_DAEMON_NAME_LEN];
	Interface *interfaces;
	uint16_t interface_num, interlink_num;
	struct Interlink *interlinks;
	void *environment;
	DaemonInfo *info;
	Dibs *dibs;
}Daemon;

typedef struct DaemonInfo{
	char language[DH_LANG_LEN], name[DH_DAEMON_NAME_LEN], scriptname[DH_DAEMON_NAME_LEN];
	Interface *interfaces;
	uint16_t interface_num;
	int trust;
}DaemonInfo;


typedef struct Interlink{
	//char srcID[DH_ID_LEN], destID[DH_ID_LEN];
	char type[DH_TYPE_LEN], format[DH_FORMAT_LEN], name[DH_INTERFACE_NAME_LEN];
	Daemon *src, *dest;
}Interlink;








void *allocList(size_t size, void* list, uint32_t *list_size){
	list = realloc(list, size*(*list_size)+1);
	list_size++;
	return ((char*)list) + size*((*list_size)-1);
}



void eraseBuffer(Buffer buf){
	free(buf.data);
}



#endif /* DOLLHOUSE_HPP_ */
