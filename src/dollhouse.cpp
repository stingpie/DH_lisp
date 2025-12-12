/*
 * dollhouse.cpp
 *
 *  Created on: Dec 4, 2025
 *      Author: stingpie
 */

/*
 * Dollhouse:
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */




#ifndef DOLLHOUSE_CPP_
#define DOLLHOUSE_CPP_

#include "dollhouse.hpp"
#include "dollhousefile.hpp"
#include <string.h>
#include <stdlib.h>
//#include <stdio.h>

#include "DH_lisp.hpp"
























#define HEAP_REALLOC_SIZE 10

LISP::LispEnv *lispDaemons;
uint8_t* lispDaemonUsage;
uint32_t lispDaemonNum=0;

Daemon* activeDaemonList;
uint8_t* activeDaemonListUsage;
uint32_t activeDaemonListLen=0;

DaemonInfo* daemonInfoList;
uint8_t* daemonInfoListUsage;
uint32_t daemonInfoListLen=0;



void bootstrap(){
	lispDaemons = (LISP::LispEnv*)malloc(sizeof(LISP::LispEnv));
	lispDaemonUsage =(uint8_t*) malloc(sizeof(uint8_t));

	activeDaemonList = (Daemon*) malloc(sizeof(Daemon));
	activeDaemonListUsage= (uint8_t*) malloc(sizeof(uint8_t));

	daemonInfoList = (DaemonInfo*) malloc(sizeof(DaemonInfo));
	daemonInfoListUsage = (uint8_t*) malloc(sizeof(uint8_t));
}










int startDaemon(const char* filename, const char* language){


	Daemon *newDaemon = (Daemon*)allocateDaemonHeap();


	strncpy(newDaemon->language, language, DH_LANG_LEN);
	strncpy(newDaemon->name, filename, DH_DAEMON_NAME_LEN);


	if(strcmp("lisp", language)==0){



		// create lispenv
		LISP::LispEnv *lispenv = (LISP::LispEnv*) allocateLispEnvHeap();
		memcpy(lispenv, LISP::NewLispEnvironment(8192, newDaemon), sizeof(LISP::LispEnv));
		newDaemon->environment = lispenv;

		// set up lispenv
		int i;
		lispenv->vars = lispenv->nil = LISP::box(LISP::NIL, 0);
		lispenv->tru = LISP::atom("#t", lispenv);
		var(1, lispenv, &lispenv->tru);                                 						// make tru a root var
		lispenv->env = LISP::env_pair(lispenv->tru, lispenv->tru, &lispenv->nil, lispenv);            // create environment with symbolic constant #t
		var(1, lispenv, &lispenv->env);                                 						// make env a root var
		for (i = 0; LISP::primitives[i].s; ++i)                   									// expand environment with primitives
		  lispenv->env = LISP::env_pair(LISP::atom(LISP::primitives[i].s, lispenv), LISP::box(LISP::PRIMITIVE, i), &lispenv->env, lispenv);


		// load script into new lisenvLISP::
		lispenv->program = DH_read(filename); // DELETE BUFFER ON LISP DAEMON EXIT

		return 1;
	}


	return 0;

	//free(scriptFilename);
}


void *allocateDaemonHeap(){
	for(uint32_t i=0; i< activeDaemonListLen; i++){
		if(activeDaemonListUsage[i]==0){

			activeDaemonListUsage[i]=1;

			return ((uint8_t*)activeDaemonList) + i;
		}
	}
	activeDaemonListLen+=HEAP_REALLOC_SIZE;
	activeDaemonListUsage = (uint8_t*)realloc(activeDaemonListUsage, sizeof(uint8_t)*activeDaemonListLen);
	activeDaemonList = (Daemon*)realloc(activeDaemonList, sizeof(Daemon)*activeDaemonListLen);

	memset(&activeDaemonListUsage[(activeDaemonListLen)-HEAP_REALLOC_SIZE], 0, sizeof(uint8_t)*HEAP_REALLOC_SIZE);
	return allocateDaemonHeap();
}

void *allocateLispEnvHeap(){
	for(uint32_t i=0; i< lispDaemonNum; i++){
		if(lispDaemonUsage[i]==0){

			lispDaemonUsage[i]=1;

			return ((uint8_t*)lispDaemons) + i;
		}
	}
	lispDaemonNum+=HEAP_REALLOC_SIZE;
	lispDaemonUsage = (uint8_t*)realloc(lispDaemonUsage, sizeof(uint8_t)*lispDaemonNum);
	lispDaemons = (LISP::LispEnv*)realloc(lispDaemons, sizeof(LISP::LispEnv)*lispDaemonNum);

	memset(&lispDaemonUsage[(lispDaemonNum)-HEAP_REALLOC_SIZE], 0, sizeof(uint8_t)*HEAP_REALLOC_SIZE);

	//printf(" alloc Usage: ");for(int k=0; k<*listLen; k++) printf(" %i ",listUsage[k]); printf("\n");

	return allocateLispEnvHeap();

}

void *allocateDaemonInfoHeap(){
	for(uint32_t i=0; i< daemonInfoListLen; i++){
		if(daemonInfoListUsage[i]==0){

			daemonInfoListUsage[i]=1;

			return ((uint8_t*)activeDaemonList) + i;
		}
	}
	daemonInfoListLen+=HEAP_REALLOC_SIZE;
	daemonInfoListUsage = (uint8_t*)realloc(daemonInfoListUsage, sizeof(uint8_t)*daemonInfoListLen);
	daemonInfoList = (DaemonInfo*)realloc(daemonInfoList, sizeof(DaemonInfo)*daemonInfoListLen);

	memset(&daemonInfoListUsage[(daemonInfoListLen)-HEAP_REALLOC_SIZE], 0, sizeof(uint8_t)*HEAP_REALLOC_SIZE);
	return allocateDaemonInfoHeap();
}




void createDaemonRegistryEntry(const char *filename){

	Buffer metadata = DH_read(filename); // this file contains the metadata about the script

	DaemonInfo newinfo;

	// split metadata file along new lines,
	// <Label>: <field>, <field> \n

	// count the number of interfaces
	unsigned int interfaces=0;
	char* lineIndex=metadata.data;
	while((lineIndex = strstr(lineIndex, "interface:")) != nullptr){ interfaces++; lineIndex++;} // count the number of interfaces.
	newinfo.interface_num=interfaces;
	newinfo.interfaces= (Interface*)calloc(sizeof(Interface), interfaces);


	// construct each interface.
	char* textcopy = (char*)calloc(sizeof(char), metadata.size);
	lineIndex=textcopy;
	memcpy(textcopy, metadata.data, metadata.size);
	uint16_t count=0;
	while((lineIndex = strstr(lineIndex, "interface:")) != nullptr){

		char *charIndex=lineIndex;
		while(*charIndex++!='\n'){ // tokenize line.
			if(*charIndex==',') *charIndex ='\0';
		}
		lineIndex = strchr(lineIndex,':')+1;
		strncpy( newinfo.interfaces[count].name, lineIndex, DH_INTERFACE_NAME_LEN);
		lineIndex = strchr(lineIndex,'\0')+1;
		strncpy( newinfo.interfaces[count].type, lineIndex, DH_TYPE_LEN);
		lineIndex = strchr(lineIndex,'\0')+1;
		strncpy( newinfo.interfaces[count].format, lineIndex, DH_FORMAT_LEN);
		newinfo.interfaces[count].direction = atoi(lineIndex); // 0 is out, 1 is in
		lineIndex = strchr(lineIndex,'\0')+1;
		newinfo.interfaces[count].triggering = atoi(lineIndex); // 0 does not trigger, 1 does.
	}

	lineIndex = strstr(metadata.data, "name:");
	char* lineEnd = strchr(lineIndex, '\n');
	uint8_t name_len = (lineEnd-lineIndex) > DH_DAEMON_NAME_LEN ?  DH_DAEMON_NAME_LEN : lineEnd-lineIndex;
	memcpy(newinfo.name, lineEnd, name_len);


	lineIndex = strstr(metadata.data, "filename:");
	name_len = (lineEnd-lineIndex) > DH_DAEMON_NAME_LEN ?  DH_DAEMON_NAME_LEN : lineEnd-lineIndex;
	memcpy(newinfo.scriptname, lineEnd, name_len);


	free(textcopy);
	eraseBuffer(metadata);

}

//keep an eye on this. A script that has two of the same interfaces, with different directions could call itself.
DaemonInfo findCorrespondingInterface(Interface *interface){

	// get a list of all possible daemons which can satisfy this interface.
	uint32_t optionsNum=0;
	DaemonInfo *optionList=(DaemonInfo*)malloc(sizeof(DaemonInfo*));
	for(unsigned int i=0; i<daemonInfoListLen; i++){
		if(daemonInfoListUsage[i]){ // if if this is a valid entry
			for(int j=0; j<daemonInfoList[i].interface_num; j++){ // look at all the interfaces for the daemon
				Interface *otherInterface = &(daemonInfoList[i].interfaces[j]);
				if( otherInterface->direction != interface->direction &&
					strncmp(otherInterface->name, interface->name, DH_INTERFACE_NAME_LEN)==0 &&
					strncmp(otherInterface->type, interface->type, DH_TYPE_LEN)==0 &&
					strncmp(otherInterface->format, interface->format, DH_FORMAT_LEN)==0
				){
					DaemonInfo* newslot = (DaemonInfo*)allocList(sizeof(DaemonInfo*), optionList, &optionsNum);
					*newslot= daemonInfoList[i]; // add pointer to daemon info to possible options
				}
			}
		}
	}

	//TODO: pick out the best one.
	DaemonInfo bestOption = optionList[0];
	free(optionList);

	return bestOption;

}


// moves data through interlink if needed.
void cycleInterlink(Interlink interlink){
	if(strcmp(interlink.src->language, "lisp")==0 ){
		LISP::LispEnv *srcEnv=(LISP::LispEnv*)(interlink.src->environment);
		if(srcEnv->outputName[0]==0  || srcEnv->output_buffer.size==0)  return; // src output buffer is empty.
		if(strcmp(interlink.dest->language, "lisp")==0){
			LISP::LispEnv * destEnv=(LISP::LispEnv*)(interlink.dest->environment);
			if(strcmp(srcEnv->outputName, interlink.name)==0){
				if(strcmp(interlink.type, "char")==0 && strcmp(interlink.format, "string")==0){

					char *buffer = (char*) calloc(sizeof(char), strlen(interlink.name) + srcEnv->output_buffer.size+strlen("(%s \"%s\")"));
					sprintf(buffer, "(%s \"%s\")", interlink.name, srcEnv->output_buffer.data);
					LISP::betterreadlisp(buffer, destEnv);
					free(buffer);
					free(srcEnv->output_buffer.data);
					srcEnv->output_buffer.size=0;
					srcEnv->outputName[0]=0;

				}

			}

		}
	}
}



void registerDaemonInterface(Interface *interface){
	interface->daemon->interface_num++;
	interface->daemon->interfaces = (Interface*)realloc(interface->daemon->interfaces, sizeof(Interface)*interface->daemon->interface_num);
}

void killDaemon(Daemon){
	// free all interfaces of daemon
	// Designate Daemonlist entry as empty.
	// erase lisp env, lisp program
}


void cycle(){

	for(int i=0; i<activeDaemonListLen; i++){ 	// run through all daemons once.
		if(activeDaemonListUsage[i]){			// if the daemon is active
			runDaemon(activeDaemonList[i]);
			for(int j=0; j<activeDaemonList[i].interlink_num; j++){ // handle IPC
				cycleInterlink(activeDaemonList[i].interlinks[j]);
			}
		}
	}


}



void runDaemon(Daemon daemon){

	if(strncmp(daemon.language, "lisp", 16)==0){
		LISP::LispEnv *env = (LISP::LispEnv*) daemon.environment;
		LISP::eval(LISP::readlisp(env), &env->env, env);
	}
}


int main(){
	bootstrap();
	startDaemon("dollhouse_sandbox/main.lisp", "lisp");

	createDaemonRegistryEntry("dollhouse_sandbox/main.proc");

	for(int k=0; k<activeDaemonListLen; k++) printf(" %i ",activeDaemonListUsage[k]); printf("\n");

	while(1){
		cycle();
		return 0;
	}

}

#endif
