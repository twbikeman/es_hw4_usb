#include <linux/init.h>
#include <linux/module.h>

MODULE_DESCRIPTION("hello-world!");
MODULE_LICENSE("GPL");


static int hello_init(void) {}


static void hello_exit(void) {}


module_init(hello_init);
module_exit(hello_exit)