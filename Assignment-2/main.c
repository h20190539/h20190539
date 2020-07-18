#include <linux/slab.h>
#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>

#define PEN_DRIVE_VID  0x0781		//enter your pendrive vid here
#define PEN_DRIVE_PID  0x558A		//enter your pendrive pid here

#define SKY_VID  0x5A1C
#define SKY_PID  0xF3C0

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])



#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24
#define READ_CAPACITY_LENGTH          0x08


// Mass Storage Requests values. See section 3 of the Bulk-Only Mass Storage Class specifications
#define BOMS_RESET                    0xFF
#define BOMS_RESET_REQ_TYPE           0x21
#define BOMS_GET_MAX_LUN              0xFE
#define BOMS_REQTYPE_LUN              0xA1
#define USB_ENDPOINT_IN				  0x80


// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
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

static int send_mass_storage_command(struct usb_device *udev, uint8_t endpoint, uint8_t lun, uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
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
		printk(KERN_INFO "send_mass_storage_command: cannot send command on IN endpoint\n");
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
	cbw->bCBWLUN = lun;
	// S->bclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);

	
	// The transfer length must always be exactly 31 bytes.
	r = usb_bulk_msg(udev, usb_sndbulkpipe(udev,endpoint), (void*)cbw, 31, &size, 1000);									//check timeout
	
	if(r!=0)
		    printk("Failed command transfer %d",r);
	    else 	
	    	printk("Read Capacity command sent successfully");

	
	printk(KERN_INFO "sent %d CDB bytes\n", cdb_len);
	printk(KERN_INFO"sent %d bytes \n",size);

	return 0;
}

static int get_mass_storage_status(struct usb_device *udev, uint8_t endpoint, uint32_t expected_tag)
{
	int r, size;
	struct command_status_wrapper *csw;
	csw=(struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev,endpoint), (void*)csw, 13, &size, 1000);
	if(r<0)
		printk("RECIEVING STATUS MESG ERROR %d",r);
	
	
	if (size != 13) {
		printk(KERN_INFO "get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk(KERN_INFO "get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printk(KERN_INFO"Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw->bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}



// Mass Storage device to test bulk transfers (non destructive test)
static int test_mass_storage(struct usb_device *udev, uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r=0, r1=0, r2=0, size;
	uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	uint32_t max_lba, block_size;
	long device_size,device_size1;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t *buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);
	
	printk("\nReset mass storage device");
	r1 = usb_control_msg(udev,usb_sndctrlpipe(udev,0),BOMS_RESET,BOMS_RESET_REQ_TYPE,0,0,NULL,0,1000);
	if(r1>=0)
		printk("successful Reset");
	else
		printk("error code: %d",r1);


	printk(KERN_INFO"Reading Max LUN:%d\n",*lun);
	r = usb_control_msg(udev, usb_sndctrlpipe(udev,0), BOMS_GET_MAX_LUN, BOMS_REQTYPE_LUN, 0, 0, (void*)lun, 1, 1000);

	// Some devices send a STALL instead of the actual value.
	// In such cases we should set lun to 0.
	if (r == 0) {
		*lun = 0;
	}else if (r < 0) {
		printk(KERN_INFO "Failed: %d\n", r);
	}
	printk(KERN_INFO "Max LUN = %d, r= %d \n", *lun,r);

	
	// Read capacity
	printk(KERN_INFO "\nReading Capacity:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;	// Read Capacity

	send_mass_storage_command(udev, endpoint_out, *lun, cdb, USB_ENDPOINT_IN, READ_CAPACITY_LENGTH, &expected_tag);     //changes to be done
	r2=usb_bulk_msg(udev, usb_rcvbulkpipe(udev,endpoint_in), (void*)buffer, 24, &size, 5000);								//check timeout
	if(r2<0)
	printk(KERN_INFO "status of r2 %d",r2);
	printk(KERN_INFO "received %d bytes\n", size);
	printk(KERN_INFO "value of &buffer[0] %d\n",buffer[0]);
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
	device_size1 = ((long)(max_lba+1))*block_size/(1024*1024);
	device_size = ((long)(max_lba+1))*block_size/(1024*1024*1024);
	printk(KERN_INFO"\nMax LBA: %08X, Block Size: %08X \n", max_lba, block_size);
	printk(KERN_INFO"Device Size: %ld MB (%ld GB)\n", device_size1, device_size);
	
	get_mass_storage_status(udev, endpoint_in, expected_tag);
	return 0;
}


static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(PEN_DRIVE_VID, PEN_DRIVE_PID)},
	{USB_DEVICE(SKY_VID, SKY_PID)},
	{} /*terminating entry*/	
};

static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	struct usb_device *udev;
	udev = interface_to_usbdev(interface);
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *endpoint;
	uint8_t endpoint_in = 0, endpoint_out = 0;

	printk(KERN_INFO "\nUAS READ Capacity Driver Inserted\n");

	if(id->idProduct == PEN_DRIVE_PID && id->idVendor == PEN_DRIVE_VID )
	{
		printk(KERN_INFO "Known USB drive detected\n");
		printk(KERN_INFO "Reading Device Descriptor:\n");

		printk(KERN_INFO "VID: %02X\n",id->idVendor);
		printk(KERN_INFO "PID: %02X\n",id->idProduct);
	}

	else if(id->idProduct == SKY_PID && id->idVendor == SKY_VID)
	{
	    printk(KERN_INFO "Known USB drive detected\n");

		printk(KERN_INFO "VID: %04X\n",id->idVendor);
		printk(KERN_INFO "PID: %04X\n",id->idProduct);
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

	            test_mass_storage(udev, endpoint_in, endpoint_out);

	return 0;
}


/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "usbdev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};


int device_init(void)
{
	usb_register(&usbdev_driver);
	return 0;
}

void device_exit(void)
{
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
	//return 0;
}

module_init(device_init);
module_exit(device_exit);

MODULE_DESCRIPTION("Assignment_2");
MODULE_AUTHOR("S PRASANNA");
MODULE_LICENSE("GPL");
