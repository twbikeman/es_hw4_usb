#include <linux/init.h>
#include <linux/module.h>

MODULE_DESCRIPTION("myusbmouse");
MODULE_LICENSE("GPL");


static int myusbmouse_init(void) {
    printk(KERN_INFO "hello world!\n");
    return 0;
}


static void myusbmouse_exit(void) {
    printk(KERN_INFO "bye!\n");
}


module_init(myusbmouse_init);
module_exit(myusbmouse_exit);