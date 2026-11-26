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
