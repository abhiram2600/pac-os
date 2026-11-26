/*
 * The symbols the kernel expects a board to define.
 *
 * Each of these is referred to by the kernel and supplied by the board, not
 * by any shared component. A board that defines none of them does not link,
 * which is where this phone was.
 *
 * None of this is a placeholder for work that belongs elsewhere. Two of the
 * three have no behaviour worth having on this board, and the third is
 * answered honestly rather than agreeably.
 */

#include <mach/boolean.h>
#include <sys/types.h>

extern "C" {

/*
 * Called on the way into exit() for every process, so that a board can
 * record the user frames of one it is interested in. Nothing here wants
 * that, and a hook that does nothing costs a call and a return.
 */
void
native_exit_trace(int pid, const char *name, int status)
{
	(void)pid;
	(void)name;
	(void)status;
}

/*
 * A board with a power manager points this at its own sleep routine, and
 * the kernel calls it when a core is parked. This phone has no suspend
 * support in our firmware, so the pointer stays null and the kernel skips
 * the call, which is what a null hook is for.
 */
void (*arm64_platform_sleep_hook)(boolean_t boot_cpu) = 0;

/*
 * Asked whether the device the kernel has chosen as its root is one this
 * board vouches for, after the kernel's own checks have already passed.
 *
 * The answer is no, and it should stay no until this board has a storage
 * driver that reads the disk and can authenticate what it read. Returning
 * yes here would not make the root trustworthy, it would only stop anyone
 * noticing that nothing had checked it. The kernel will refuse the root and
 * say so, which is the correct outcome for a board whose storage driver
 * does not exist yet.
 */
int
native_verified_block_root_authorized(dev_t device)
{
	(void)device;
	return 0;
}

}

/*
 * The stack allocation alignment shift, read by Apple's own code.
 *
 * Apple's entitlement parser checks it is still inside its stack with
 * sp & ((1 << shift) - 1), and takes an error exit if the result is small.
 * That assumes the stack base is aligned to 1 << shift, which holds on
 * Apple's 16 KiB-page kernels. This kernel uses 4 KiB pages, where the stack
 * starts 8 KiB into a 32 KiB-aligned allocation, so handing that code the
 * page shift makes it decide it has run off a perfectly good stack. It then
 * fails to copy an entitlement dictionary while starting the first user
 * process, and the system stops there.
 *
 * 15 is the allocation alignment, not the page size, which is what the check
 * actually needs: with it, the masked stack pointer stays at least 8 KiB
 * inside any stack. Everything else in the kernel keeps using the page shift
 * for page arithmetic.
 *
 * The prelink step binds Apple's reference to this name rather than to its
 * own constant.
 */
extern "C" int unixnu_kernel_stack_shift = 15;
