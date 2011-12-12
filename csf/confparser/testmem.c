#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "confparser.h"


#define COMMAND_MAX 1024
int
main(void)
{
	char command[COMMAND_MAX + 1];
	int timestamp=0;
	struct conf_str_config conf_str_array[] = {
		{"command", command}, 
		{0, 0}
	};
	struct conf_int_config conf_int_array[] = {
		{"timestamp", &timestamp},
		{0,0} 
	};
	dictionary	*conf;
	char buf[] = "[command]\r\n\ncommand=\"ls\"\r\ntimestamp=11111\r\n";
	conf = open_conf_mem(buf, strlen(buf));
	if (conf == NULL) {
		fprintf(stderr,"errror");
	}
	parse_conf_file(conf, "command", conf_int_array, conf_str_array);
	close_conf_file(conf);
	printf("%s\n%d\n", command,timestamp);
}
