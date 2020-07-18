# Block Driver for USB_Device
Simple linux module explanation for usb storage with SCSI protocol

Pre-requisite:
    You have to ensure that your Produce ID & Vendor ID is correct by command "lsusb" and this code will work only in kernel less than 5.0
    
How to run
1. Open 2 terminals, using cd <folder_name> to access the folder where the code is present and other 'dmesg -wH' to see the kernel log.
2. type 'make all' to compile the program.
3. insert the kernal file using 'sudo insmod main.ko'.
4. remove existing usb driver using 'sudo rmmod uas' and 'sudo rmmod usb_storage'.
5. Plug usb flash device and check if usb details is coming or not
6. type 'sudo fdisk -l' to see if the usb_driver is seen or not... here it will show /dev/usb1. Then proceed further.
7. create a folder in media directory using 'sudo mkdir /media/folder_name/' Ex: sudo mkdir /media/kusb/
8. type 'sudo mount -t vfat /dev/myusb1 /media/folder_name' to mount the usb filesystem into /media/foldername. Ex: sudo mount -t vfat /dev/myusb1 /media/kusb/
9. go to root directory by typing 'sudo -i'
10. go to the directory where usb_driver is mounted. Ex: cd /media/kusb/
11. create a .txt file by typing 'echo "write something" >text_file_name.txt'.
12. With the help of command 'cat text_file_name.txt', you can see the content in the text file.
13. Leave from the media directory using 'cd ../..' and unmount the file system using 'umount /media/folder_name/' Ex: umount /media/kusb/
14. to leave the root directory, type 'logout'.
