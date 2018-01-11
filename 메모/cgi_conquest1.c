#include <stdio.h>
//

//방문자 카운트 프로그램
main()
{
	int count;
	FILE *fpnt;

	fpnt = fopen( "count.dat", "r");
	fscanf(fpnt, "%d", &count);
	fclose(fpnt);

	count++;

	fpnt = fopen( "count.dat", "w");
	fprintf(fpnt, "%d", count);
	fclose(fpnt);

	printf("Content-type:text/html \n\n");
	printf("<html>\n<body>\n");
	printf("%d번째 방문객입니다.\n", count);
	printf("</body>\n</html>\n");
}