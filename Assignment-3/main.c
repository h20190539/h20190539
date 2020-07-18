#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/slab.h>
#include<linux/usb.h>
#include<linux/blkdev.h>
#include<linux/genhd.h>
#include<linux/spinlock.h>
#include<linux/bio.h>
#include<linux/fs.h>
#include<linux/interrupt.h>
#include<linux/workqueue.h>
#include<linux/sched.h>

#define PEN_DRIVE_VID  0x0781		//enter your pendrive vid here
#define PEN_DRIVE_PID  0x558A		//enter your pendrive pid here

#define DEVICE_NAME "myusb"
#define BOMS_GET_MAX_LUN              0xFE
#define BOMS_REQTYPE_LUN              0xA1
#define READ_CAPACITY_LENGTH	      0x08
#define USB_ENDPOINT_IN		      0x80
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

#define bio_iovec_idx(bio, idx)	(&((bio)->bi_io_vec[(idx)]))
#define __bio_kmap_atomic(bio, idx, kmtype)				\
	(kmap_atomic(bio_iovec_idx((bio), (idx))->bv_page) +	\
		bio_iovec_idx((bio), (idx))->bv_offset)
#define __bio_kunmap_atomic(addr, kmtype) kunmap_atomic(addr)


struct gendisk *usb_disk = NULL;
int32_t total_sectors;
int dev_read_capacity(void);
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
}; 



 static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
}; 


static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(PEN_DRIVE_VID, PEN_DRIVE_PID)},
	{} /*terminating entry*/	
};

struct usb_device *udev;
uint8_t endpoint_in , endpoint_out ;

struct blkdev_private{
        int size;                       /* Device size in sectors */
        u8 *data;                       /* The data array */
        spinlock_t lock;                /* For mutual exclusion */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
      };	


struct request *req;
static struct blkdev_private *dev = NULL;

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USB Device is Removed\n");
	struct gendisk *usb_disk = dev->gd;
	del_gendisk(usb_disk);
	blk_cleanup_queue(dev->queue);
	kfree(dev);
	return;
}

//////////////////// function to send SCSI command (cdb) in CBW//////////////////////////////////////////
static int send_mass_storage_command(struct usb_device *udev,uint8_t endpoint, uint8_t lun,
                         uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int r, size;
	struct command_block_wrapper *cbw;
	cbw=(struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);
	
	if (cdb == NULL) {
		return -1;
	}
	if (endpoint & USB_ENDPOINT_IN) {
		printk("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}	
	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk(KERN_INFO "send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}	

	memset(cbw, 0, sizeof(*cbw));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN =0;
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);
	

	r = usb_bulk_msg(udev,usb_sndbulkpipe(udev,endpoint),(void*)cbw,31, &size,1000);
	if(r!=0)
		printk("Failed command transfer %d",r);
	
	return 0;
} 

/////////////////////////////function to receive csw/////////////////////////////////////////////
static int get_mass_storage_status(struct usb_device *udev, uint8_t endpoint, uint32_t expected_tag)
{	
	int r, size;
	struct command_status_wrapper *csw;
	csw=(struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);
	r=usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint),(void*)csw,13, &size, 1000);
	if(r<0)
		printk("RECIEVING STATUS MESG ERROR %d",r);
	
	if (size != 13) {
		printk("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}	
	return 0;
}  

/////////////////////////////Function for implementing READ SCSI Command/////////////////////////////////////
static int dev_read(sector_t initial_sector,sector_t nr_sect,char *page_address)
{
int result;
unsigned int size;
uint8_t cdb[16];	// SCSI Command Descriptor block
uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
uint32_t expected_tag;
size=0;

memset(cdb,0,sizeof(cdb));
cdb[0] = 0x28;	// Read(10)
cdb[2]=(initial_sector>>24) & 0xFF;
cdb[3]=(initial_sector>>16) & 0xFF;
cdb[4]=(initial_sector>>8) & 0xFF;
cdb[5]=(initial_sector>>0) & 0xFF;
cdb[7]=(nr_sect>>8) & 0xFF;
cdb[8]=(nr_sect>>0) & 0xFF;	

send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_ENDPOINT_IN,(nr_sect*512),&expected_tag);
result=usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint_in),(unsigned char*)(page_address),(nr_sect*512),&size, 5000);
if(page_address[510]==0x55 && page_address[511]==0xffffffaa)
{
printk("Reading MBR");
printk("buffer[510]: %x",page_address[510]);
printk("buffer[511]: %x",page_address[511]&0xFF);
}
else
{
printk("Reading from Sector:");
}
get_mass_storage_status(udev, endpoint_in, expected_tag);  
return 0;
}
////////////////////////////Function for implementing WRITE SCSI comamand////////////////////////////
 static int dev_write(sector_t initial_sector,sector_t nr_sect,char *page_address)
	{   
	int result;
	unsigned int size;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	
	printk(KERN_INFO "Writing into sector:");
	memset(cdb,0,sizeof(cdb));
	cdb[0]=0x2A;
	cdb[2]=(initial_sector>>24)&0xFF;
	cdb[3]=(initial_sector>>16)&0xFF;
	cdb[4]=(initial_sector>>8)&0xFF;
	cdb[5]=(initial_sector>>0)&0xFF;
	cdb[7]=(nr_sect>>8)&0xFF;
	cdb[8]=(nr_sect>>0)&0xFF;	// 1 block
	//cdb[8]=0x01;
	send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_DIR_OUT,nr_sect*512,&expected_tag);
	result=usb_bulk_msg(udev,usb_sndbulkpipe(udev,endpoint_out),(unsigned char*)page_address,nr_sect*512,&size, 1000);
	
	get_mass_storage_status(udev, endpoint_in, expected_tag); 
	return 0;

}  
/////////////////////Function to perform READ or WRITE/////////////////////////////////
static void rb_transfer(sector_t sector,sector_t nsect, char *buffer, int write)
{
    unsigned long offset = sector*512;
    unsigned long nbytes = nsect*512;

    if ((offset + nbytes) > (total_sectors*512)) {
        printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
        return;
    }
     if (write)
        dev_write(sector,nsect,buffer);
    else
        dev_read(sector,nsect,buffer);
    return; 
} 
////////////////////////Request Function////////////////////////
static int send_req(struct request *req)
{
    
    int i;
    sector_t address;
    int dir = rq_data_dir(req);
    struct bio_vec bvec;
    struct req_iterator iter;
    sector_t sector_offset;
    unsigned int sectors;
    sector_t starting_sector = blk_rq_pos(req);
    
    sector_t sector = req->bio->bi_iter.bi_sector;
    sector_offset = 0;
   

    rq_for_each_segment(bvec,req,iter){
        sectors = bvec.bv_len / 512;
		address = starting_sector+sector_offset;
    	char *buffer = __bio_kmap_atomic(req->bio, i, KM_USER0);
    	rb_transfer(address,sectors,buffer, dir==WRITE);
    	sector_offset += sectors;
    	__bio_kunmap_atomic(req->bio, KM_USER0);
	printk(KERN_DEBUG "Starting Sector: %llu, Sector Offset: %llu; Total sectors: %u sectors\n",\
		(unsigned long long)(starting_sector), (unsigned long long)(sector_offset), sectors);
    }
    return 0; 
}  

static struct workqueue_struct *my_queue=NULL; ////global variable for workqueue//////

/////////////////////private structure, since bottom half function takes only one argument///////////////////////	
struct usb_work{   
	struct work_struct work; // kernel structure
	struct request *req;
 };
//////////////////Function for the work to be performed in bottom half/////////////////////
static void delayed_work_function(struct work_struct *work)
{
	struct usb_work *usb_work=container_of(work,struct usb_work,work); ////to access our private structure/////////
	send_req(usb_work->req);
	__blk_end_request_cur(usb_work->req,0);
	kfree(usb_work);
	return;
}
/////////////////////request handling funnction///////////////////////////////
void usb_request(struct request_queue *q)   
{
	struct request *req;  
	struct usb_work *usb_work=NULL;
  	
	while((req=blk_fetch_request(q)) != NULL)
	{
		if(req == NULL && !blk_rq_is_passthrough(req)) 
			{
				printk(KERN_INFO "non FS request");
				__blk_end_request_all(req, -EIO);
				continue;
			}
		usb_work=(struct usb_work *)kmalloc(sizeof(struct usb_work),GFP_ATOMIC);
		if(usb_work==NULL)
		{
			printk("Memory Allocation to deferred work is failed");
			__blk_end_request_all(req, 0);
			continue;
		}

		usb_work->req=req;
		INIT_WORK(&usb_work->work,delayed_work_function);
		queue_work(my_queue,&usb_work->work);   /////queueing the work/////
		
	}	
} 
//////////////////////////////block device operation function for open///////////////////////////
static int my_open(struct block_device *bdev, fmode_t mode)
{
    struct blkdev_private *dev = bdev->bd_disk->private_data;
    spin_lock(&dev->lock);
    spin_unlock(&dev->lock);
    return 0;
}
/////////////////////////////block device operation for release (close)///////////////////////////////
static void my_release(struct gendisk *gdisk, fmode_t mode)
{
    struct blkdev_private *dev = gdisk->private_data;
    spin_lock(&dev->lock);
    spin_unlock(&dev->lock);

    //return 0;
}

static struct block_device_operations blkdev_ops =
{
	.owner= THIS_MODULE,
	.open=my_open,
	.release=my_release
};
//////////////////////READ CAPACITY SCSI COMMAND////////////////
//////////////////////This is done to get info abot device such as size, number of sectors etc. ///////////////////
int dev_read_capacity()
{
int size;
uint8_t cdb[16];	// SCSI Command Descriptor block
uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
uint32_t expected_tag;
uint32_t block_size;
long device_size;
uint8_t *buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);
size=0;

usb_control_msg(udev, usb_sndctrlpipe(udev,0), BOMS_GET_MAX_LUN, BOMS_REQTYPE_LUN, 0, 0, (void*)lun, 1, 1000);


// Read capacity
printk(KERN_INFO "\nReading Device Capacity:\n");
memset(buffer, 0, sizeof(buffer));
memset(cdb, 0, sizeof(cdb));
cdb[0] = 0x25;	// Read Capacity

send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_ENDPOINT_IN,READ_CAPACITY_LENGTH,&expected_tag);
usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint_in),(void*)buffer, 24,&size, 5000);

total_sectors = be_to_int32(&buffer[0]);
block_size = be_to_int32(&buffer[4]);
device_size = ((long)(total_sectors+1))*block_size/(1024*1024*1024);
printk(KERN_INFO"Total number of sectors: %ld\n", total_sectors);
printk(KERN_INFO"Device Size: %ld GB (%ld MB)\n\n", device_size,device_size*1024);


return 0;
}


static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{	
	int i;
	unsigned char epAddr, epAttr;
	udev=interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *endpoint;
	

	if(id->idProduct == PEN_DRIVE_PID && id->idVendor == PEN_DRIVE_VID )
	{
		printk(KERN_INFO "Known USB drive detected\n");
		printk(KERN_INFO "Reading Device Descriptor:\n");

		printk(KERN_INFO "VID: %02X\n",id->idVendor);
		printk(KERN_INFO "PID: %02X\n",id->idProduct);
	}
	else
	{
		printk(KERN_INFO "\nUnknown device plugged_in\n");
	}
	

	printk(KERN_INFO "Interface Class: %02X\n",interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "Interface SubClass: %02X \n",interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "Interface Protocol: %02X\n",interface->cur_altsetting->desc.bInterfaceProtocol);

	if ((interface->cur_altsetting->desc.bInterfaceClass == 0x08)
			  && (interface->cur_altsetting->desc.bInterfaceSubClass == 0x06 )
			  && (interface->cur_altsetting->desc.bInterfaceProtocol == 0x50) ) 
			{
				// Mass storage devices that can use basic SCSI commands
				printk(KERN_INFO"This is a valid SCSI device \n");
			}

	printk(KERN_INFO "number of endpoints= %d\n",interface->cur_altsetting->desc.bNumEndpoints);
			

	for (i=0; i<interface->cur_altsetting->desc.bNumEndpoints; i++) 
			{
				endpoint = &interface->cur_altsetting->endpoint[i].desc;
				
				// Use the first interrupt or bulk IN/OUT endpoints as default for testing
				
			 	epAddr = endpoint->bEndpointAddress;
	        	epAttr = endpoint->bmAttributes;
	
		        if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		        {
			       if(epAddr & 0x80)
				     {   
				     	 endpoint_in= endpoint->bEndpointAddress;
				         printk(KERN_INFO "EP %d is Bulk IN\n", i);
				     }
			       else
				      {   
				      	  endpoint_out= endpoint->bEndpointAddress;
    				      printk(KERN_INFO "EP %d is Bulk OUT\n", i);
				      }
	
		        } 

		    printk(KERN_INFO "endpoint[%d]'s address: %02X\n", i, endpoint->bEndpointAddress);
  
			}
	
	dev_read_capacity();

	
	int c=0;
	c = register_blkdev(0, "usb_disk");  // major no. allocation
	if (c < 0) 
		printk(KERN_WARNING "usb_disk: unable to get major number\n");
	
	dev = kmalloc(sizeof(struct blkdev_private),GFP_KERNEL); // allocation of private structre  
	
	if(!dev)
	{
		printk("ENOMEM  at %d\n",__LINE__);
		return 0;
	}
	memset(dev, 0, sizeof(struct blkdev_private)); // initializes all device value as 0

	spin_lock_init(&dev->lock);  // spin lock is initialised and the lock is held during initialization and manipulation of req queue
	
	
	dev->queue = blk_init_queue(usb_request, &dev->lock);  // performs queue operation when called by usb request
   
	usb_disk = dev->gd = alloc_disk(2); 
	if(!usb_disk)
	{
		kfree(dev);
		printk(KERN_INFO "alloc_disk failed\n");
		return 0;
	}
	// gendisk structure
	usb_disk->major =c;
	usb_disk->first_minor = 0;
	usb_disk->fops = &blkdev_ops;
	usb_disk->queue = dev->queue;
	usb_disk->private_data = dev;
	strcpy(usb_disk->disk_name, DEVICE_NAME);
	set_capacity(usb_disk,total_sectors); 
	add_disk(usb_disk);  // after initialisation, add disk now

return 0;
}

/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "usbdev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};

int block_init(void)
{
	usb_register(&usbdev_driver);
	printk(KERN_NOTICE "UAS READ Capacity Driver Inserted\n");
	printk(KERN_INFO "USB Registered\n"); 
	my_queue=create_workqueue("my_queue");  // worker thread name
	return 0;	
}

void block_exit(void)
{ 
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
	flush_workqueue(my_queue);  // exits the work done
	destroy_workqueue(my_queue);
	return;
}


module_init(block_init);
module_exit(block_exit);

MODULE_DESCRIPTION("Assignment-3");
MODULE_AUTHOR("S PRASANNA, KARTIKE SINGH GAUR");
MODULE_LICENSE("GPL");
