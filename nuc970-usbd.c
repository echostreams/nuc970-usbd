/* ########################################################################

   USBIP hardware emulation
   https://github.com/lcgamboa/USBIP-Virtual-USB-Device

   ########################################################################

   Copyright (c) : 2016  Luis Claudio Gambôa Lopes

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   For e-mail suggestions :  lcgamboa@yahoo.com
   ######################################################################## */

/*
 * Usage:
 * nuc970-usbd  # port:3240, bus:1-1
 * sudo usbip --tcp-port 3240 attach -r 127.0.0.1 -b 1-1
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "usbip.h"
#include "usbd.h"

typedef struct endpoint_regs {
    unsigned int dat;       // Data Register
    unsigned int intsts;    // Interrupt Status
    unsigned int inten;     // Interrupt Enable
    unsigned int datcnt;    // Data Available Count
    unsigned int rspctl;    // Response Control
    unsigned int mps;       // Maximum Packet Size
    unsigned int txcnt;     // Transfer Count
    unsigned int cfg;       // Configuration
    unsigned int bufstart;  // RAM Start Address
    unsigned int bufend;    // RAM End Address
} Endpoint_Regs;

typedef struct usbd_state
{
    unsigned int gintsts;   // Global Interrupt Status

    unsigned int ginten;    // Global Interrupt Enable
    unsigned int busintsts; // Bus Interrupt Status
    unsigned int businten;  // Bus Interrupt Enable
    unsigned int oper;      // USB Operational
    unsigned int framecnt;  // Frame Count
    unsigned int faddr;     // USB Function Address
    unsigned int test;      // Test Mode
    unsigned int cepdat;
    unsigned int cepinten;
    unsigned int cepintsts;
    unsigned int ceptxcnt;
    unsigned int ceprxcnt;
    unsigned int cepdatcnt;
    unsigned int setup1_0;
    unsigned int setup3_2;
    unsigned int setup5_4;
    unsigned int setup7_6;
    unsigned int cepbufstart;
    unsigned int cepbufend;
    unsigned int dmactl;
    unsigned int dmacnt;
    Endpoint_Regs endpoints[12];// A~L 12 configurable endpoints
    unsigned int dmaaddr;       // AHB DMA Address Register 0x700
    unsigned int phyctl;        // USB PHY Control Register 0x704
} Usbd_State;

Usbd_State usbd_regs;

   /* USB Descriptor Type */
#define DESC_DEVICE         0x01
#define DESC_CONFIG         0x02
#define DESC_STRING         0x03
#define DESC_INTERFACE      0x04
#define DESC_ENDPOINT       0x05
#define DESC_QUALIFIER      0x06
#define DESC_OTHERSPEED     0x07
#define DESC_IFPOWER        0x08
#define DESC_OTG            0x09

/* USB Descriptor Length */
#define LEN_DEVICE          18
#define LEN_QUALIFIER       10
#define LEN_CONFIG          9
#define LEN_INTERFACE       9
#define LEN_ENDPOINT        7
#define LEN_OTG             5
#define LEN_HID             9

/* USB Endpoint Type */
#define EP_ISO              0x01
#define EP_BULK             0x02
#define EP_INT              0x03

#define EP_INPUT            0x80
#define EP_OUTPUT           0x00

/* Device Descriptor */
const USB_DEVICE_DESCRIPTOR dev_dsc =
{
    0x12,                   // Size of this descriptor in bytes
    0x01,                   // DEVICE descriptor type
    0x0200,                 // USB Spec Release Number in BCD format
    0x00,                   // Class Code
    0x00,                   // Subclass code
    0x00,                   // Protocol code
    0x40,                   // Max packet size for EP0, see usb_config.h
    0x0416,                 // Vendor ID
    0x5963,                 // Product ID
    0x0100,                 // Device release number in BCD format
    0x01,                   // Manufacturer string index
    0x02,                   // Product string index
    0x00,                   // Device serial number string index
    0x01                    // Number of possible configurations
};

const USB_DEVICE_QUALIFIER_DESCRIPTOR dev_qua = {
    0x0A,       // bLength
    0x06,       // bDescriptorType
    0x0200,     // bcdUSB
    0x00,       // bDeviceClass
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    0x40,       // bMaxPacketSize
    0x01,       // iSerialNumber
    0x00        //bNumConfigurations*/
};

//Configuration
typedef struct __attribute__((__packed__)) _CONFIG_MSC
{
    USB_CONFIGURATION_DESCRIPTOR dev_conf0;
    USB_INTERFACE_DESCRIPTOR dev_int0;
    USB_ENDPOINT_DESCRIPTOR dev_ep0;
    USB_ENDPOINT_DESCRIPTOR dev_ep1;
} CONFIG_MSC;

const CONFIG_MSC  configuration_msc = {
    {
        /* Configuration Descriptor */
        0x09,//sizeof(USB_CFG_DSC),    // Size of this descriptor in bytes
        USB_DESCRIPTOR_CONFIGURATION,                // CONFIGURATION descriptor type
        0x0020, //sizeof(CONFIG_CDC),                // Total length of data for this cfg
        1,                      // Number of interfaces in this cfg
        1,                      // Index value of this configuration
        0,                      // Configuration string index
        0xC0,
        50,                     // Max power consumption (2X mA)
        },
        {
            /* Interface Descriptor */
            0x09,//sizeof(USB_INTF_DSC),   // Size of this descriptor in bytes
            USB_DESCRIPTOR_INTERFACE,               // INTERFACE descriptor type
            0,                      // Interface Number
            0,                      // Alternate Setting Number
            2,                      // Number of endpoints in this intf
            0,                      // Class code
            0,                      // Subclass code
            0,                      // Protocol code
            0                       // Interface string index
            },
            {
                /* Endpoint Descriptor */
                0x07,/*sizeof(USB_EP_DSC)*/
                USB_DESCRIPTOR_ENDPOINT,    //Endpoint Descriptor
                0x81,                       //EndpointAddress
                0x02,                       //Attributes
                0x0200,                     //size
                0x01                        //Interval
                },{
                    /* Endpoint Descriptor */
                    0x07,/*sizeof(USB_EP_DSC)*/
                    USB_DESCRIPTOR_ENDPOINT,    //Endpoint Descriptor
                    0x02,                       //EndpointAddress
                    0x02,                       //Attributes
                    0x0200,                     //size
                    0x01                        //Interval
                }
};

const unsigned char string_0[] = { // available languages descriptor
                0x04,
                USB_DESCRIPTOR_STRING,
                0x09,
                0x04
};

const unsigned char string_1[] = { //
                0x16,
                USB_DESCRIPTOR_STRING, // bLength, bDscType
                'U', 0x00, //
                'S', 0x00, //
                'B', 0x00, //
                ' ', 0x00, //
                'D', 0x00, //
                'e', 0x00, //
                'v', 0x00, //
                'i', 0x00, //
                'c', 0x00, //
                'e', 0x00, //
};

const unsigned char string_2[] = { //
                0x34,
                USB_DESCRIPTOR_STRING, //
                'N', 0x00, //
                'u', 0x00, //
                'v', 0x00, //
                'o', 0x00, //
                't', 0x00, //
                'o', 0x00, //
                'n', 0x00, //
                ' ', 0x00, //
                'A', 0x00, //
                'R', 0x00, //
                'M', 0x00, //
                ' ', 0x00, //
                '9', 0x00, //
                '2', 0x00, //
                '6', 0x00, //
                '-', 0x00, //
                'B', 0x00, //
                'a', 0x00, //
                's', 0x00, //
                'e', 0x00, //
                'd', 0x00, //
                ' ', 0x00, //
                'M', 0x00, //
                'C', 0x00, //
                'U', 0x00, //
};


const char* configuration = (const char*)&configuration_msc;

const USB_INTERFACE_DESCRIPTOR* interfaces[] = { &configuration_msc.dev_int0 };

const unsigned char* strings[] = { string_0, string_1, string_2 };


#define BSIZE 4096
char buffer[BSIZE + 1];
int  bsize = 0;

void hex_dump(unsigned char *buf, int size)
{
    int i;
    for (i = 0; i < size; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}

void handle_data(int sockfd, USBIP_RET_SUBMIT* usb_req, int bl)
{

    printf(">> Data EP: %d, len: %d\n", usb_req->ep, bl);
    int ack;
    if (usb_req->ep == 0x01)
    {
        printf("EP1 received \n");

        if (usb_req->direction == 0) //input
        {
            printf("direction=input\n");
            bsize = recv(sockfd, (char*)buffer, bl, 0);
            
            //ack = 0x55aa55aa;
            //send_usb_req(sockfd, usb_req, (char*)&ack, 4, 0);

            buffer[bsize + 1] = 0; //string terminator
            //printf("received (%s)\n", buffer);
            hex_dump(buffer, bsize);
            //send_usb_req(sockfd, usb_req, "", 0, 0);
        }
        else
        {
            if (_usbd_flash_type == USBD_FLASH_SPI)
            {
                ack = 100;
            }
            else if (bsize != 400) // XUSB and others
            {
                ack = bsize;
            }
            else {
                ack = 0x55aa55aa;   // DDR
            }

            printf("direction=output, flash type: %d, ack: %08x\n", _usbd_flash_type, ack);

            send_usb_req(sockfd, usb_req, (char*)&ack, 4, 0);

        }
        usleep(500);
    }

    if ((usb_req->ep == 0x02))
    {
        printf("EP2 received \n");
        if (usb_req->direction == 0) //input
        {
            printf("direction=input\n");
            bsize = recv(sockfd, (char*)buffer, bl, 0);

            //ack = 0x55aa55aa;
            //send_usb_req(sockfd, usb_req, (char*)&ack, 4, 0);
            send_usb_req(sockfd, usb_req, "", 0, 0);
            
            buffer[bsize + 1] = 0; //string terminator
            //printf("received (%s)\n", buffer);
            hex_dump(buffer, bsize);
        }
        else //output
        {
            printf("direction=output\n");
            if (bsize != 0)
            {
                int i;
                for (i = 0; i < bsize; i++)//increment received char
                    buffer[i] += 1;
                hex_dump(buffer, bsize);
                send_usb_req(sockfd, usb_req, buffer, bsize, 0);
                //printf("sending (%s)\n", buffer);
                bsize = 0;
            }
            else
            {
                send_usb_req(sockfd, usb_req, "", 0, 0);
                usleep(500);
                printf("no data disponible\n");
            }
        }
    }

};


typedef struct _LINE_CODING
{
    word dwDTERate;  //in bits per second
    byte bCharFormat;//0-1 stop; 1-1.5 stop; 2-2 stop bits
    byte ParityType; //0 none; 1- odd; 2 -even; 3-mark; 4 -space
    byte bDataBits;  //5,6,7,8 or 16
}LINE_CODING;

LINE_CODING linec;

unsigned short linecs = 0;

void handle_unknown_control(int sockfd, StandardDeviceRequest* control_req, USBIP_RET_SUBMIT* usb_req)
{

    printf(">> Control Type: %d, %d\n", control_req->bmRequestType, control_req->bRequest);
    // vendor command
    USB_CMD_T _usb_cmd_pkt;
    memcpy(&_usb_cmd_pkt, control_req, sizeof(USB_CMD_T));

    if (_usb_cmd_pkt.bmRequestType == 0x40)
    {
        if (_usb_cmd_pkt.bRequest == 0xa0)
        {
            if (_usb_cmd_pkt.wValue == 0x12) {
                Bulk_Out_Transfer_Size = _usb_cmd_pkt.wIndex;
                printf(" >> Bulk_Out_Transfer_Size: %d\n", Bulk_Out_Transfer_Size);
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
            }
            else if (_usb_cmd_pkt.wValue == 0x13) {
                // reset DMA
                outpw(REG_USBD_DMA_CTRL_STS, 0x80);
                outpw(REG_USBD_DMA_CTRL_STS, 0x00);
                outpw(REG_USBD_EPA_RSP_SC, 0x01);		// flush fifo
                outpw(REG_USBD_EPB_RSP_SC, 0x01);		// flush fifo

                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete
            }
            send_usb_req(sockfd, usb_req, "", 0, 0);
        }

        if (_usb_cmd_pkt.bRequest == 0xb0) {
            if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_SDRAM)) {
                _usbd_flash_type = USBD_FLASH_SDRAM;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH SDRAM\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_NAND)) {
                _usbd_flash_type = USBD_FLASH_NAND;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH NAND\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_NAND_RAW)) {
                _usbd_flash_type = USBD_FLASH_NAND_RAW;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH NAND RAW\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_MMC)) {
                _usbd_flash_type = USBD_FLASH_MMC;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH MMC\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_MMC_RAW)) {
                _usbd_flash_type = USBD_FLASH_MMC_RAW;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH MMC RAW\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_SPI)) {
                _usbd_flash_type = USBD_FLASH_SPI;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH SPI\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_FLASH_SPI_RAW)) {
                _usbd_flash_type = USBD_FLASH_SPI_RAW;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH SPI RAW\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_MTP)) {
                _usbd_flash_type = USBD_MTP;
                outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
                printf(" >> FLASH MTP\n");
            }
            else if (_usb_cmd_pkt.wValue == (USBD_BURN_TYPE + USBD_INFO)) {
                _usbd_flash_type = USBD_INFO;
                printf(" >> FLASH USBD_INFO\n");
            }
            outpw(REG_USBD_CEP_CTRL_STAT, CEP_NAK_CLEAR);	// clear nak so that sts stage is complete//lsshi
            send_usb_req(sockfd, usb_req, "", 0, 0);
        }

        outpw(REG_USBD_CEP_IRQ_STAT, 0x400);
        return;

    }

    /* vendor IN for ack */
    if (_usb_cmd_pkt.bmRequestType == 0xc0) {
        // clear flags
        //usbdClearAllFlags();

        //GET_VEN_Flag = 1;
        outpw(REG_USBD_CEP_IRQ_STAT, 0x408);
        outpw(REG_USBD_CEP_IRQ_ENB, 0x408);		//suppkt int ,status and in token
        int ack = _usb_cmd_pkt.wValue;
        send_usb_req(sockfd, usb_req, (char*)&ack, 4, 0);
        return;
    }

#if 0
    if (control_req->bmRequestType == 0x21)//Abstract Control Model Requests
    {
        if (control_req->bRequest == 0x20)  //SET_LINE_CODING
        {
            printf("SET_LINE_CODING\n");
            if ((recv(sockfd, (char*)&linec, control_req->wLength, 0)) != control_req->wLength)
            {
                printf("receive error : %s \n", strerror(errno));
                exit(-1);
            };
            send_usb_req(sockfd, usb_req, "", 0, 0);
        }
        if (control_req->bRequest == 0x21)  //GET_LINE_CODING
        {
            printf("GET_LINE_CODING\n");
            send_usb_req(sockfd, usb_req, (char*)&linec, 7, 0);
        }
        if (control_req->bRequest == 0x22)  //SET_LINE_CONTROL_STATE
        {
            linecs = control_req->wValue0;
            printf("SET_LINE_CONTROL_STATE 0x%02X\n", linecs);
            send_usb_req(sockfd, usb_req, "", 0, 0);
        }
        if (control_req->bRequest == 0x23)  //SEND_BREAK
        {
            printf("SEND_BREAK\n");
            send_usb_req(sockfd, usb_req, "", 0, 0);
        }
    }
#endif
};

int main()
{
    printf("nuc970 usbd started....\n");
    usbip_run(&dev_dsc);
}