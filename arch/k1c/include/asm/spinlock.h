/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SPINLOCK_H
#define _ASM_K1C_SPINLOCK_H

#include <linux/kernel.h>

#include <asm/spinlock_types.h>

#include <linux/atomic.h>

union ticket_spinlock {
	uint64_t val;
	struct {
		uint32_t current_ticket;
		uint32_t next_ticket;
	};
};

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	union ticket_spinlock tmp = {.val = lock->lock};

	BUILD_BUG_ON(sizeof(union ticket_spinlock) != 8);

	return (tmp.current_ticket != tmp.next_ticket);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	union ticket_spinlock old_ticket, new_ticket;

	/* Increment the next serving ticket */
	do {
		old_ticket = (union ticket_spinlock) lock->lock;
		new_ticket = old_ticket;
		new_ticket.current_ticket =
					old_ticket.current_ticket + 1;

	} while (cmpxchg(&lock->lock, old_ticket.val,
		  new_ticket.val) != old_ticket.val);
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	union ticket_spinlock old_ticket, new_ticket, tmp_ticket;

	/* Load the spinlock values */
	old_ticket.val = lock->lock;

	/* is spinlock already held ? */
	if (old_ticket.next_ticket != old_ticket.current_ticket)
		return 0;

	new_ticket = old_ticket;
	new_ticket.next_ticket = old_ticket.next_ticket + 1;

	tmp_ticket.val = cmpxchg(&lock->lock, old_ticket.val,
			       new_ticket.val);

	if (tmp_ticket.val == old_ticket.val)
		return 1;

	return 0;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	union ticket_spinlock old_ticket, new_ticket, tmp_ticket;

	/* atomically get and increment the ticket number */
	do {
		old_ticket = (union ticket_spinlock) lock->lock;
		new_ticket = old_ticket;
		new_ticket.next_ticket = old_ticket.next_ticket + 1;

	} while (cmpxchg(&lock->lock, old_ticket.val,
		  new_ticket.val) != old_ticket.val);

	/* We atomically get a ticket, now, see if it's our turn */
	while (1) {
		tmp_ticket = (union ticket_spinlock) lock->lock;
		/* is our ticket equals to the current one ? */
		if (tmp_ticket.current_ticket == old_ticket.next_ticket)
			break;
		/* if not, spin */
	};
}

#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

/**********************************************************
 *		ReadWrite lock
 ***********************************************************/

static inline void arch_rwlock_init(arch_rwlock_t *rw)
{
}

static inline int arch_read_can_lock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	return (atomic_read(count) > 0);
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	while (atomic_dec_return(count) < 0)
		atomic_inc(count);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	atomic_inc(count);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	atomic_dec(count);
	if (atomic_read(count) >= 0)
		return 1;

	atomic_inc(count);

	return 0;
}

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)

static inline int arch_write_can_lock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	return (atomic_read(count) == RW_LOCK_BIAS);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	while (!arch_write_can_lock(rw))
		asm volatile("nop;;");

	while (!atomic_sub_and_test(RW_LOCK_BIAS, count)) {
		atomic_add(RW_LOCK_BIAS, count);
		while (!arch_write_can_lock(rw))
			asm volatile("nop;;");
	}
}

#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	atomic_add(RW_LOCK_BIAS, count);
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	atomic_t *count = (atomic_t *) rw;

	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;

	atomic_add(RW_LOCK_BIAS, count);

	return 0;
}

#endif
