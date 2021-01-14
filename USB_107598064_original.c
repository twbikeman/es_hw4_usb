// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  USB HIDBP Mouse support
 */

/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

/* for apple IDs */
#ifdef CONFIG_USB_HID_MODULE
#include "../hid-ids.h"
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.6"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB HID Boot Protocol mouse driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

//滑鼠結構

struct usb_mouse {

	//滑鼠名稱
	char name[128];
	//節點名稱
	char phys[64];

	//usb裝置指標
	struct usb_device *usbdev;
	//輸入設備指標
	struct input_dev *dev;

	//urb請求指標
	struct urb *irq;
	//一般傳輸指標
	signed char *data;
	//dma傳輸位址
	dma_addr_t data_dma;
};
//irb callback函數
static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *dev = mouse->dev;
	int status;
	// status 值為０表示urb成功
	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	//除了以上三種錯誤外，將重新呼叫urb
	default:		/* error */
		goto resubmit;
	}
	//向子系統回報滑鼠事件
	// bit 0,1,2,3,4 分別代表左,右,中,SIDE,EXTRA鍵按下
	input_report_key(dev, BTN_LEFT,   data[0] & 0x01);
	input_report_key(dev, BTN_RIGHT,  data[0] & 0x02);
	input_report_key(dev, BTN_MIDDLE, data[0] & 0x04);
	input_report_key(dev, BTN_SIDE,   data[0] & 0x08);
	input_report_key(dev, BTN_EXTRA,  data[0] & 0x10);
	//data[1]滑鼠水平位移
	input_report_rel(dev, REL_X,     data[1]);
	//data[２]滑鼠垂直位移
	input_report_rel(dev, REL_Y,     data[2]);
	//data[２]滑鼠wheel位移
	input_report_rel(dev, REL_WHEEL, data[3]);
	//事件同步
	input_sync(dev);
resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		dev_err(&mouse->usbdev->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			mouse->usbdev->bus->bus_name,
			mouse->usbdev->devpath, status);
}

//開啟滑鼠設備時,呼叫probe中建構的urb,進入urb生命周期

static int usb_mouse_open(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);

	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

//關閉滑鼠設備時,結束urb生命周期

static void usb_mouse_close(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);

	usb_kill_urb(mouse->irq);
}

//驅動程式的probe函數

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	//利用interface得到接口interface及endpoint
	struct usb_device *dev = interface_to_usbdev(intf);
	//usb_host_interface接口設備
	struct usb_host_interface *interface;
	//usb_endpoint_descriptor是端點描述符結構
	struct usb_endpoint_descriptor *endpoint;
	struct usb_mouse *mouse;
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;
	interface = intf->cur_altsetting;
	//滑鼠有１個interrupt IN　的　endpoint
	//不符合的均會回報錯誤
	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;
	

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);

	//取得端點能夠傳輸的最大數據

	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	//為mouse設備分配內存

	mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!mouse || !input_dev)
		goto fail1;

	mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
	if (!mouse->data)
		goto fail1;

	mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mouse->irq)
		goto fail2;
//GFP_ATOMIC表示不等待，GFP_KERNEL是普通的優先，可以休眠等待，由於滑鼠使用中斷傳輸方法
//，不允許休眠，data是周期性獲取滑鼠事的儲存區，因此使用GFP_ATOMIC優先等級

	mouse->usbdev = dev;
	mouse->dev = input_dev;
//取得滑鼠設備的名稱
	if (dev->manufacturer)
		strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(mouse->name, " ", sizeof(mouse->name));
		strlcat(mouse->name, dev->product, sizeof(mouse->name));
	}

	if (!strlen(mouse->name))
		snprintf(mouse->name, sizeof(mouse->name),
			 "USB HIDBP Mouse %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));

//填入滑鼠設備結構的節點名稱，usb_make_path用來取後usb設備在Sysfs中的路徑

	usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
	strlcat(mouse->phys, "/input0", sizeof(mouse->phys));
//將滑鼠設備的名稱給輸入系統結構
	input_dev->name = mouse->name;
//將滑鼠設備節點給輸入統統結構
	input_dev->phys = mouse->phys;
//input_dev->id，用來儲存廠商、設備類型和設備的編號
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;
//evbit描述事件，EV_KEY是按鍵事件，EV_REL是相對座標事件
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
//keybit表示鍵值，包含左鍵、右鍵及中鍵
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
//relbit用於表示相對座標值
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |
		BIT_MASK(BTN_EXTRA);
//中鍵滾動的滾動值
	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
//將滑鼠結構給input_dev的private數值
	input_set_drvdata(input_dev, mouse);
//設定輸入設備打開函數指標
	input_dev->open = usb_mouse_open;
//設定輸入設備關閉函數指標
	input_dev->close = usb_mouse_close;

	usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,
			 (maxp > 8 ? 8 : maxp),
			 usb_mouse_irq, mouse, endpoint->bInterval);
	mouse->irq->transfer_dma = mouse->data_dma;
	mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

//向系統註冊輸入設備

	error = input_register_device(mouse->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, mouse);
	return 0;

fail3:	
	usb_free_urb(mouse->irq);
fail2:	
	usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
fail1:	
	input_free_device(input_dev);
	kfree(mouse);
	return error;
}

//滑鼠拔除時的處理程序

static void usb_mouse_disconnect(struct usb_interface *intf)
{
	//獲取滑鼠設備結構
	
	struct usb_mouse *mouse = usb_get_intfdata (intf);

	//將接口結構中的滑鼠指標置空
	usb_set_intfdata(intf, NULL);
	if (mouse) {
		//結束urb生命周期
		usb_kill_urb(mouse->irq);
		//註銷滑鼠設備
		input_unregister_device(mouse->dev);
		//釋放urb記憶空間
		usb_free_urb(mouse->irq);
		//釋放滑鼠事件記憶空間
		usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
		//釋放存放滑鼠結構空間
		kfree(mouse);
	}
}

//usb_device_id結構用於表示該驅動設備所技持的設備
//USB_INTERFACE_INFO可以用來匹配特定類型的接口
//參數意思為(類別，子類別，協定)
//USB_INTERFEACE_CLASS_ID_HID 人機交互設備類別
//USB_INTERFACE_SUBCLASS_BOOT boot階段使用的HID
//SUB_INTERFACE_PROTOCOL_MOUSE 滑鼠的協定

static const struct usb_device_id usb_mouse_id_table[] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

//用來讓使用者空間的程序知道這個驅動程序能夠支持的設備，第一個參數必須是usb

MODULE_DEVICE_TABLE (usb, usb_mouse_id_table);


//滑鼠驅動程序結構


static struct usb_driver usb_mouse_driver = {
	.name		= "usbmouse",
	.probe		= usb_mouse_probe,
	.disconnect	= usb_mouse_disconnect,
	.id_table	= usb_mouse_id_table,
};

module_usb_driver(usb_mouse_driver);
