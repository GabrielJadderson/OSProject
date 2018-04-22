#ifndef _DM510_H_
#define _DM510_H_

/*
* Ioctl definitions
*/
#define DM510_IOC_MAGIC  'o'

#define DM510_IOCRESET    _IO(DM510_IOC_MAGIC, 0)

#define DM510_SET_BUFFER _IOW(DM510_IOC_MAGIC,  1, int)
#define DM510_SET_READERS   _IOW(DM510_IOC_MAGIC,  2, int)
#define DM510_PRINT_AUTHORS _IOR(DM510_IOC_MAGIC,   3)

#define DM510_P_IOCTSIZE _IO(DM510_IOC_MAGIC,   4)
#define DM510_P_IOCQSIZE _IO(DM510_IOC_MAGIC,   5)

#define SCULL_IOC_MAXNR 5



#endif 