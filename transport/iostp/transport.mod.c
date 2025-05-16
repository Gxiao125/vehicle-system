#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xfa985410, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0xe664169e, __VMLINUX_SYMBOL_STR(unregister_flexcan_transport) },
	{ 0x761c877c, __VMLINUX_SYMBOL_STR(register_flexcan_transport) },
	{ 0x72027224, __VMLINUX_SYMBOL_STR(init_net) },
	{ 0x84ae869b, __VMLINUX_SYMBOL_STR(flexcan_transport_send) },
	{ 0x67921cca, __VMLINUX_SYMBOL_STR(dev_get_by_name) },
	{ 0xefd6cf06, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr0) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "244FDFFA89ACD99092BD22C");
