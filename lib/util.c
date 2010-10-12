#include <inc/util.h>

static int digit(char c, int base)
{
	if (base == 10) {
		if (c<='9' && c>='0')
			return c-'0';
	} else if (base == 16) {
		if (c<='9' && c>='0')
			return c-'0';
		else if(c<='f' && c>='a')
			return c-'a'+10;
	}
	return -1;

}

uint32_t atoi(char *str)
{
	int base = 10, dig;
	int i = 0;
	uint32_t num = 0;

	if (str[0]=='0' && str[1]=='x') {
		base = 16;
		i = 2;
	}
	
	for ( ; str[i]!='\0'; i++) {
		dig = digit(str[i], base);
		if (dig >= 0) {
			num *= base;
			num += dig;
		} else {
			return -1;
		}
	}
	return num;
}
