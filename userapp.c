#include <stdio.h> 
#include<stdlib.h>
#include<fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include<sys/ioctl.h>
#define adc "/dev/adc"

#define WR_VALUE_1 _IOW('a','a',int32_t*)
#define WR_VALUE_2 _IOW('a','b',int32_t*)

int main()
{ 
  while(1)
  {
  int p;
  int fd;
  uint16_t val;
  uint32_t num,allign;
  fd=open(adc, O_RDWR);
  
  if(fd  == -1)
  {
  
    printf("cannot read source file adc\n");
    return 0;
  }

    printf("Enter the ADC Channel No. (0-7):-\n");
    scanf("%d",&num);
     if (num >= 8 )
    {
    printf("WARNING : Enter the proper ADC Channel No.\n");
    break;
    }
    else
    {
    ioctl(fd, WR_VALUE_1, (int32_t*) &num); 
    }

    printf("Enter the Allignment No. (0:Lower 10-Bits Allignment, 1:Higher 10-Bits Allignment):-\n");
    scanf("%d",&allign);
    if (allign >= 2 )
    {
    printf("WARNING : Enter proper Allignment No.\n");
    break;
    }
    else
    {
    ioctl(fd, WR_VALUE_2, (int32_t*) &allign);
    } 
  
  read(fd,&val,2);
  printf("ADC value: %d\n",val);

  printf("Continue (Yes-1, No-2): \n");
  scanf("%d",&p);

  if (p==1)
  {
  continue;
  }
  else if(p==2)
  {
    break;
  }
  else
  {
    break;
  }

  close(fd);
 } 
  return 0;
}
