#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Get_query(char *buf, char *field, char *result)
{
    int cnt;
    char *find;
    
    find = strstr(buf, field);
    find = find + strlen(field);
    for(cnt=0; (find[cnt] != '&')&&(find[cnt] != '\0'); cnt++)
        result[cnt] = find[cnt];
    result[cnt] = '\0';
}

int main(int argc, char const *argv[])
{
	/* code */
	int dan, soo;
	char query[10], d[4];

	strcpy(query, getenv("QUERY_STRING"));

	Get_query(query, "d=", d);
	dan = atoi(d);

	printf("context-type:text/html\n\n");
	printf("<html>\n<body>\n");
	printf("<table border=1>\n<tr>\n");
	printf("<td>\n");
	for(soo=1; soo < 10; soo++)
		printf("%d X %d = %d<br>", dan, soo, dan*soo);
	printf("</td>\n");
	printf("</tr>\n</table>\n");
	printf("</body>\n</html>\n");

	return 0;
}