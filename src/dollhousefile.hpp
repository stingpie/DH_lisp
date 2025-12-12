/*
 * dollhousefile.hpp
 *
 *  Created on: Nov 30, 2025
 *      Author: stingpie
 */

#ifndef DOLLHOUSEFILE_HPP_
#define DOLLHOUSEFILE_HPP_

#include <stdio.h>
#include "dollhouse.hpp"
#include <string.h>

#include <unistd.h>

#include <sys/stat.h>


#define DOLLHOUSE_SANDBOX_DIR "dollhouse_sandbox/"



int IsInSandbox(const char* filename){
	return  true;//strncmp(DOLLHOUSE_SANDBOX_DIR, filename, strlen(DOLLHOUSE_SANDBOX_DIR))==0 ? 1 : 0;
}


Buffer DH_read(const char* filename){
	if(IsInSandbox(filename)){
		Buffer new_buffer={0};
		FILE *fp = fopen(filename, "r");
		if(fp==nullptr){
			Buffer failed_buffer={0};
			failed_buffer.size=0;
			return failed_buffer;
		}
		struct stat st;
		stat(filename, &st);
		unsigned int size = st.st_size;
		new_buffer.size = size;
		new_buffer.data = (char*)calloc(sizeof(char), size);
		fgets(new_buffer.data, size, fp);
		fclose(fp);
		return new_buffer;
	} else{
		Buffer failed_buffer={0};
		failed_buffer.size=0;
		return failed_buffer;
	}
};

int DH_write(const char* filename, Buffer buf){
	if(IsInSandbox(filename)){
		FILE *fp =fopen(filename, "w");
		if(fp==NULL) return -1;
		fwrite(buf.data, sizeof(char), buf.size, fp);
		fclose(fp);
		return 0;
	} else{
		return -1;
	}
}

int DH_append(const char* filename, Buffer buf){
	if(IsInSandbox(filename)){
		FILE *fp =fopen(filename, "a");
		if(fp==NULL) return -1;
		fwrite(buf.data, sizeof(char), buf.size, fp);
		fclose(fp);
		return 0;
	} else {
		return -1;
	}
}

int DH_create(const char *filename){
	if(IsInSandbox(filename) && !access(filename, 'r')){
		FILE *fp = fopen(filename, "w");
		fclose(fp);
		return 0;
	}else{
		return -1;
	}
}

int DH_delete_file(const char *filename){
	if(IsInSandbox(filename) && access(filename, 'r')){
		return remove(filename);
	} else{
		return -1;
	}
}

int DH_alias(const char *filename, const char *alias){
	if(IsInSandbox(filename) && access(filename, 'r')){
		return link(filename, alias);
	} else{
		return -1;
	}
}

// too complicated and has minimal benefit.
/*
int make_directory(const char *dir_name){
	if(IsInSandbox(filename)){
		return mkdir();
	}
}
*/

#endif /* DOLLHOUSEFILE_HPP_ */
